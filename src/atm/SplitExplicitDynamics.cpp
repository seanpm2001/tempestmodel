///////////////////////////////////////////////////////////////////////////////
///
///	\file    SplitExplicitDynamics.cpp
///	\author  Paul Ullrich
///	\version September 19, 2013
///
///	<remarks>
///		Copyright 2000-2010 Paul Ullrich
///
///		This file is distributed as part of the Tempest source code package.
///		Permission is granted to use, copy, modify and distribute this
///		source code and its documentation under the terms of the GNU General
///		Public License.  This software is provided "as is" without express
///		or implied warranty.
///	</remarks>

#include "Defines.h"
#include "SplitExplicitDynamics.h"
#include "PhysicalConstants.h"
#include "Model.h"
#include "Grid.h"
#include "GridGLL.h"
#include "GridPatchGLL.h"

#include "Announce.h"
#include "LinearAlgebra.h"
#include "FunctionTimer.h"

//#define FIX_ELEMENT_MASS_NONHYDRO

///////////////////////////////////////////////////////////////////////////////

SplitExplicitDynamics::SplitExplicitDynamics(
	Model & model,
	int nHorizontalOrder,
	int nHyperviscosityOrder,
	double dNuScalar,
	double dNuDiv,
	double dNuVort,
	double dInstepNuDiv
) :
	HorizontalDynamics(model),
	m_nHorizontalOrder(nHorizontalOrder),
	m_nHyperviscosityOrder(nHyperviscosityOrder),
	m_dNuScalar(dNuScalar),
	m_dNuDiv(dNuDiv),
	m_dNuVort(dNuVort),
	m_dInstepNuDiv(dInstepNuDiv),
	m_dBd(0.1),
	m_dBs(0.1)
{
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::Initialize() {

#if !defined(PROGNOSTIC_CONTRAVARIANT_MOMENTA)
	_EXCEPTIONT("Prognostic covariant velocities not supported");
#endif

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());
	if (pGrid == NULL) {
		_EXCEPTIONT("Grid must be of type GridGLL");
	}

	// Number of vertical levels
	const int nRElements = pGrid->GetRElements();

	// Number of tracers
	const int nTracerCount = m_model.GetEquationSet().GetTracers();

	// Vertical implicit terms (A)
	m_dA.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Vertical implicit terms (B)
	m_dB.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Vertical implicit terms (C)
	m_dC.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Vertical implicit terms (D)
	m_dD.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the diagnosed 2D kinetic energy
	m_dK2.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Initialize the diagnosed 2D contravariant alpha velocity
	m_d2DConUa.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Initialize the diagnosed 2D contravariant beta velocity
	m_d2DConUb.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Initialize the diagnosed 2D covariant alpha velocity
	m_d2DCovUa.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Initialize the diagnosed 2D covariant beta velocity
	m_d2DCovUb.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Contravariant vertical momentum on interfaces
	m_dSDotREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the diagnosed vertial alpha momentum flux
	m_dSDotUaREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the diagnosed vertial beta momentum flux
	m_dSDotUbREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the diagnosed vertical theta flux
	m_dSDotThetaREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the diagnosed vertical vertical momentum flux
	m_dSDotWNode.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Initialize the alpha and beta mass fluxes
	m_dAlphaMassFlux.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	m_dBetaMassFlux.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	m_dZMassFluxREdge1.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	m_dZMassFluxREdge2.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the alpha and beta momentum fluxes
	m_dAlphaVerticalMomentumFluxREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	m_dBetaVerticalMomentumFluxREdge.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Initialize the alpha and beta pressure fluxes
	m_dAlphaPressureFlux.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	m_dBetaPressureFlux.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	m_dZPressureFluxREdge1.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	m_dZPressureFluxREdge2.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements+1);

	// Nodel mass update, used in vertical implicit calculation
	m_dNodalMassUpdate.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Nodel pressure update, used in vertical implicit calculation
	m_dNodalPressureUpdate.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Acoustic pressure term used in vertical update
	m_dAcousticPressureTerm.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder,
		nRElements);

	// Buffer state
	m_dBufferState.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder);
	
	// Initialize buffers for derivatives of Jacobian
	m_dJGradientA.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder);

	m_dJGradientB.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder);

	// LAPACK info structure
	m_nInfo.Allocate(
		m_nHorizontalOrder,
		m_nHorizontalOrder);
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::FilterNegativeTracers(
	int iDataUpdate
) {
#ifdef POSITIVE_DEFINITE_FILTER_TRACERS
	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical elements
	const int nRElements = pGrid->GetRElements();

	// Perform local update
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		const DataArray3D<double> & dElementAreaNode =
			pPatch->GetElementAreaNode();

		DataArray4D<double> & dataUpdateTracer =
			pPatch->GetDataTracers(iDataUpdate);

		// Number of tracers
		const int nTracerCount = dataUpdateTracer.GetSize(0);

		// Get number of finite elements in each coordinate direction
		int nElementCountA = pPatch->GetElementCountA();
		int nElementCountB = pPatch->GetElementCountB();

		// Loop over all elements
		for (int a = 0; a < nElementCountA; a++) {
		for (int b = 0; b < nElementCountB; b++) {

			// Loop overall tracers and vertical levels
			for (int c = 0; c < nTracerCount; c++) {
			for (int k = 0; k < nRElements; k++) {

				// Compute total mass and non-negative mass
				double dTotalInitialMass = 0.0;
				double dTotalMass = 0.0;
				double dNonNegativeMass = 0.0;

				for (int i = 0; i < m_nHorizontalOrder; i++) {
				for (int j = 0; j < m_nHorizontalOrder; j++) {

					int iA = a * m_nHorizontalOrder + i + box.GetHaloElements();
					int iB = b * m_nHorizontalOrder + j + box.GetHaloElements();

					double dPointwiseMass =
						  dataUpdateTracer[c][iA][iB][k]
						* dElementAreaNode[iA][iB][k];

					dTotalMass += dPointwiseMass;

					if (dataUpdateTracer[c][iA][iB][k] >= 0.0) {
						dNonNegativeMass += dPointwiseMass;
					}
				}
				}
/*
				// Check for negative total mass
				if (dTotalMass < 1.0e-14) {
					printf("%i %i %i %1.15e %1.15e\n", n, a, b, dTotalInitialMass, dTotalMass);
					_EXCEPTIONT("Negative element mass detected in filter");
				}
*/
				// Apply scaling ratio to points with non-negative mass
				double dR = dTotalMass / dNonNegativeMass;

				for (int i = 0; i < m_nHorizontalOrder; i++) {
				for (int j = 0; j < m_nHorizontalOrder; j++) {

					int iA = a * m_nHorizontalOrder + i + box.GetHaloElements();
					int iB = b * m_nHorizontalOrder + j + box.GetHaloElements();

					if (dataUpdateTracer[c][iA][iB][k] > 0.0) {
						dataUpdateTracer[c][iA][iB][k] *= dR;
					} else {
						dataUpdateTracer[c][iA][iB][k] = 0.0;
					}
				}
				}
			}
			}
		}
		}
	}
#endif
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::CalculateTendencies(
	int iDataInitial,
	int iDataTendencies,
	double dDeltaT
) {
	// Start the function timer
	//FunctionTimer timer("CalculateTendencies");

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical elements
	const int nRElements = pGrid->GetRElements();

	// Physical constants
	const PhysicalConstants & phys = m_model.GetPhysicalConstants();

	// Indices of EquationSet variables
	const int UIx = 0;
	const int VIx = 1;
	const int PIx = 2;
	const int WIx = 3;
	const int RIx = 4;

	// Perform local update
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		const DataArray3D<double> & dElementAreaNode =
			pPatch->GetElementAreaNode();
		const DataArray2D<double> & dJacobian2D =
			pPatch->GetJacobian2D();
		const DataArray3D<double> & dJacobian =
			pPatch->GetJacobian();
		const DataArray3D<double> & dJacobianREdge =
			pPatch->GetJacobianREdge();
		const DataArray3D<double> & dCovMetric2DA =
			pPatch->GetCovMetric2DA();
		const DataArray3D<double> & dCovMetric2DB =
			pPatch->GetCovMetric2DB();
		const DataArray3D<double> & dContraMetric2DA =
			pPatch->GetContraMetric2DA();
		const DataArray3D<double> & dContraMetric2DB =
			pPatch->GetContraMetric2DB();

		const DataArray4D<double> & dDerivRNode =
			pPatch->GetDerivRNode();
		const DataArray4D<double> & dDerivRREdge =
			pPatch->GetDerivRREdge();

		const DataArray3D<double> & dataZn =
			pPatch->GetZLevels();
		const DataArray3D<double> & dataZi =
			pPatch->GetZInterfaces();

		const DataArray2D<double> & dCoriolisF =
			pPatch->GetCoriolisF();

		// Data
		DataArray4D<double> & dataInitialNode =
			pPatch->GetDataState(iDataInitial, DataLocation_Node);

		DataArray4D<double> & dataTendenciesNode =
			pPatch->GetDataState(iDataTendencies, DataLocation_Node);

		DataArray4D<double> & dataInitialREdge =
			pPatch->GetDataState(iDataInitial, DataLocation_REdge);

		DataArray4D<double> & dataTendenciesREdge =
			pPatch->GetDataState(iDataTendencies, DataLocation_REdge);

		const DataArray3D<double> & dataPressure =
			pPatch->GetDataPressure();

		// Element grid spacing and derivative coefficients
		const double dElementDeltaA = pPatch->GetElementDeltaA();
		const double dElementDeltaB = pPatch->GetElementDeltaB();

		const double dInvElementDeltaA = 1.0 / dElementDeltaA;
		const double dInvElementDeltaB = 1.0 / dElementDeltaB;

		const DataArray2D<double> & dDxBasis1D = pGrid->GetDxBasis1D();
		const DataArray2D<double> & dStiffness1D = pGrid->GetStiffness1D();

		// Get number of finite elements in each coordinate direction
		const int nElementCountA = pPatch->GetElementCountA();
		const int nElementCountB = pPatch->GetElementCountB();

		// Loop over all elements
		for (int a = 0; a < nElementCountA; a++) {
		for (int b = 0; b < nElementCountB; b++) {

			const int iElementA = a * m_nHorizontalOrder + box.GetHaloElements();
			const int iElementB = b * m_nHorizontalOrder + box.GetHaloElements();

			// Perform interpolation from levels to interfaces and calculate
			// auxiliary quantities on model interfaces.
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 1; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				dataInitialREdge[RIx][iA][iB][k] = 0.5 * (
					  dataInitialNode[RIx][iA][iB][k-1]
					+ dataInitialNode[RIx][iA][iB][k  ]);

				const double dInvRhoREdge =
					1.0 / dataInitialREdge[RIx][iA][iB][k];

				dataInitialREdge[UIx][iA][iB][k] = 0.5 * (
					  dataInitialNode[UIx][iA][iB][k-1]
					+ dataInitialNode[UIx][iA][iB][k  ]);

				dataInitialREdge[VIx][iA][iB][k] = 0.5 * (
					  dataInitialNode[VIx][iA][iB][k-1]
					+ dataInitialNode[VIx][iA][iB][k  ]);

				// Note that we store theta not rhotheta
				dataInitialREdge[PIx][i][j][k] =
					0.5 * dInvRhoREdge *
						(dataInitialNode[PIx][i][j][k-1]
						+ dataInitialNode[PIx][i][j][k]);

				// Vertical flux of conserved quantities
				m_dSDotREdge[i][j][k] =
					dataInitialREdge[WIx][iA][iB][k]
						- dataInitialREdge[UIx][iA][iB][k]
							* dDerivRREdge[iA][iB][k][0]
						- dataInitialREdge[VIx][iA][iB][k]
							* dDerivRREdge[iA][iB][k][1];

				const double dSDotInvRhoREdge =
					m_dSDotREdge[i][j][k] * dInvRhoREdge;

				m_dSDotUaREdge[i][j][k] =
					dSDotInvRhoREdge
					* dataInitialREdge[UIx][iA][iB][k];

				m_dSDotUbREdge[i][j][k] =
					dSDotInvRhoREdge
					* dataInitialREdge[VIx][iA][iB][k];

				m_dSDotThetaREdge[i][j][k] =
					m_dSDotREdge[i][j][k]
					* dataInitialREdge[PIx][iA][iB][k];

				// Horizontal vertical momentum flux
				const double dVerticalMomentumBaseFluxREdge =
					dataInitialREdge[WIx][iA][iB][k]
					* dInvRhoREdge;

				m_dAlphaVerticalMomentumFluxREdge[i][j][k] =
					dVerticalMomentumBaseFluxREdge
					* dataInitialREdge[UIx][iA][iB][k];

				m_dBetaVerticalMomentumFluxREdge[i][j][k] =
					dVerticalMomentumBaseFluxREdge
					* dataInitialREdge[VIx][iA][iB][k];
			}
			}
			}

			// Calculate auxiliary quantities on model levels
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				const double dRhoUa = dataInitialNode[UIx][iA][iB][k];
				const double dRhoUb = dataInitialNode[VIx][iA][iB][k];
				const double dRhoWREdge = dataInitialREdge[WIx][iA][iB][k];

				const double dInvRho = 1.0 / dataInitialNode[RIx][iA][iB][k];

				// 2D contravariant and covariant velocities
				m_d2DConUa[i][j][k] = dInvRho * dRhoUa;
				m_d2DConUb[i][j][k] = dInvRho * dRhoUb;

				m_d2DCovUa[i][j][k] =
					  dCovMetric2DA[iA][iB][0] * m_d2DConUa[i][j][k]
					+ dCovMetric2DA[iA][iB][1] * m_d2DConUb[i][j][k];

				m_d2DCovUb[i][j][k] =
					  dCovMetric2DB[iA][iB][0] * m_d2DConUa[i][j][k]
					+ dCovMetric2DB[iA][iB][1] * m_d2DConUb[i][j][k];

				// Horizontal mass fluxes
				m_dAlphaMassFlux[i][j][k] =
					dJacobian[iA][iB][k] * dRhoUa;

				m_dBetaMassFlux[i][j][k] =
					dJacobian[iA][iB][k] * dRhoUb;

				// Horizontal pressure fluxes
				m_dAlphaPressureFlux[i][j][k] =
					m_dAlphaMassFlux[i][j][k]
					* dataInitialNode[PIx][iA][iB][k]
					* dInvRho;

				m_dBetaPressureFlux[i][j][k] =
					m_dBetaMassFlux[i][j][k]
					* dataInitialNode[PIx][iA][iB][k]
					* dInvRho;

				// 2D Kinetic energy
				m_dK2[i][j][k] = 0.5 * (
					  m_d2DCovUa[i][j][k] * m_d2DConUa[i][j][k]
					+ m_d2DCovUb[i][j][k] * m_d2DConUb[i][j][k]);

				// Vertical flux of momentum
				m_dSDotWNode[i][j][k] =
					0.5 * (dataInitialREdge[WIx][iA][iB][k]
						+ dataInitialREdge[WIx][iA][iB][k])
					- dDerivRNode[iA][iB][k][0] * dataInitialNode[UIx][iA][iB][k]
					- dDerivRNode[iA][iB][k][1] * dataInitialNode[VIx][iA][iB][k];
			}
			}
			}

			// Calculate nodal updates
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				// Horizontal pressure derivatives
				double dDaP = 0.0;
				double dDbP = 0.0;

				// Derivatives of mass flux
				double dDaMassFluxAlpha = 0.0;
				double dDbMassFluxBeta = 0.0;

				// Derivatives of rhotheta flux
				double dDaPressureFluxAlpha = 0.0;
				double dDbPressureFluxBeta = 0.0;

				// Derivative of the specific kinetic energy
				double dDaKE = 0.0;
				double dDbKE = 0.0;

				// Derivatives of the 2D covariant velocity
				double dDaCovUb = 0.0;
				double dDbCovUa = 0.0;

				// Alpha derivatives
				for (int s = 0; s < m_nHorizontalOrder; s++) {

					// Alpha derivative of pressure
					dDaP +=
						dataPressure[iElementA+s][iB][k]
						* dDxBasis1D[s][i];

					// Alpha derivative of mass flux
					dDaMassFluxAlpha -=
						m_dAlphaMassFlux[s][j][k]
						* dStiffness1D[i][s];

					// Alpha derivative of mass flux
					dDaPressureFluxAlpha -=
						m_dAlphaPressureFlux[s][j][k]
						* dStiffness1D[i][s];

					// Alpha derivative of specific kinetic energy
					dDaKE +=
						m_dK2[s][j][k]
						* dDxBasis1D[s][i];

					// Alpha derivative of 2D covariant velocity
					dDaCovUb +=
						m_d2DCovUb[s][j][k]
						* dDxBasis1D[s][i];
				}

				// Beta derivatives
				for (int s = 0; s < m_nHorizontalOrder; s++) {

					// Beta derivative of pressure
					dDbP +=
						dataPressure[iA][iElementB+s][k]
						* dDxBasis1D[s][j];

					// Beta derivative of mass flux
					dDbMassFluxBeta -=
						m_dBetaMassFlux[i][s][k]
						* dStiffness1D[j][s];

					// Beta derivative of mass flux
					dDbPressureFluxBeta -=
						m_dBetaPressureFlux[i][s][k]
						* dStiffness1D[j][s];

					// Beta derivative of specific kinetic energy
					dDbKE +=
						m_dK2[i][s][k]
						* dDxBasis1D[s][j];

					// Beta derivative of 2D covariant velocity
					dDbCovUa +=
						m_d2DCovUa[i][s][k]
						* dDxBasis1D[s][j];
				}

				// Scale derivatives
				dDaP *= dInvElementDeltaA;
				dDbP *= dInvElementDeltaB;

				dDaMassFluxAlpha *= dInvElementDeltaA;
				dDbMassFluxBeta *= dInvElementDeltaB;

				dDaPressureFluxAlpha *= dInvElementDeltaA;
				dDbPressureFluxBeta *= dInvElementDeltaB;

				dDaKE *= dInvElementDeltaA;
				dDbKE *= dInvElementDeltaB;

				dDaCovUb *= dInvElementDeltaA;
				dDbCovUa *= dInvElementDeltaB;

				// Convert derivatives of pressure along s surface
				// to derivatives along z surfaces.
				double dDzP = 0.0;
				if (k == 0) {
					dDzP = (dataPressure[i][j][k+1]
						- dataPressure[i][j][k])
						/ (dataZn[iA][iB][k+1] - dataZn[iA][iB][k]);

				} else if (k == nRElements-1) {
					dDzP = (dataPressure[i][j][k]
						- dataPressure[i][j][k-1])
						/ (dataZn[iA][iB][k] - dataZn[iA][iB][k-1]);

				} else {
					dDzP = (dataPressure[i][j][k+1]
						- dataPressure[i][j][k-1])
						/ (dataZn[iA][iB][k+1] - dataZn[iA][iB][k-1]);
				}

				dDaP -= dDerivRNode[iA][iB][k][0] * dDzP;
				dDbP -= dDerivRNode[iA][iB][k][1] * dDzP;

				// Convert pressure derivatives to contravariant
				const double dConDaP =
					  dContraMetric2DA[iA][iB][0] * dDaP
					+ dContraMetric2DA[iA][iB][1] * dDbP;

				const double dConDbP =
					  dContraMetric2DB[iA][iB][0] * dDaP
					+ dContraMetric2DB[iA][iB][1] * dDbP;

				// Convert Kinetic energy derivatives to contravariant
				const double dConDaKE =
					  dContraMetric2DA[iA][iB][0] * dDaKE
					+ dContraMetric2DA[iA][iB][1] * dDbKE;

				const double dConDbKE =
					  dContraMetric2DB[iA][iB][0] * dDaKE
					+ dContraMetric2DB[iA][iB][1] * dDbKE;

				// Compute terms in tendency equation
				const double dInvJacobian =
					1.0 / dJacobian[iA][iB][k];

				const double dInvJacobian2D =
					1.0 / dJacobian2D[iA][iB];

				const double dInvDeltaZ =
					1.0 / (dataZi[iA][iB][k+1] - dataZi[iA][iB][k]);

				// Total horizontal flux divergence
				const double dTotalHorizFluxDiv =
					dInvJacobian * (
						  dDaMassFluxAlpha
						+ dDbMassFluxBeta);

				// Vertical fluxes
				const double dDsAlphaMomentumFluxS =
					dInvDeltaZ * (
						  m_dSDotUaREdge[i][j][k+1]
						- m_dSDotUaREdge[i][j][k  ]);

				const double dDsBetaMomentumFluxS =
					dInvDeltaZ * (
						  m_dSDotUbREdge[i][j][k+1]
						- m_dSDotUbREdge[i][j][k  ]);

				const double dDzMassFluxVertical =
					dInvDeltaZ * (
						  m_dSDotREdge[i][j][k+1]
						- m_dSDotREdge[i][j][k  ]);

				const double dDzPressureFluxVertical =
					dInvDeltaZ * (
						  m_dSDotThetaREdge[i][j][k+1]
						- m_dSDotThetaREdge[i][j][k  ]);

				// Compute vorticity term
				const double dAbsVorticity = 
					dCoriolisF[iA][iB]
					+ dInvJacobian2D * (dDaCovUb - dDbCovUa);

				const double dVorticityAlpha =
					- dAbsVorticity * dInvJacobian2D * m_d2DCovUb[i][j][k];

				const double dVorticityBeta =
					  dAbsVorticity * dInvJacobian2D * m_d2DCovUa[i][j][k];

				// Compose slow tendencies
				dataTendenciesNode[UIx][iA][iB][k] =
					- dConDaP
					- dataInitialNode[RIx][iA][iB][k] * dConDaKE
					- dTotalHorizFluxDiv * m_d2DConUa[i][j][k]
					- dDsAlphaMomentumFluxS
					- dVorticityAlpha;
/*
				printf("%1.5e %1.5e %1.5e %1.5e %1.5e : %1.5e\n",
					- dConDaP,
					- dataInitialNode[RIx][iA][iB][k] * dConDaKE,
					- dTotalHorizFluxDiv * m_d2DConUa[i][j][k],
					- dDsAlphaMomentumFluxS,
					- dVorticityAlpha,
					dataTendenciesNode[UIx][iA][iB][k]);
*/
				dataTendenciesNode[VIx][iA][iB][k] =
					- dConDbP
					- dataInitialNode[RIx][iA][iB][k] * dConDbKE
					- dTotalHorizFluxDiv * m_d2DConUb[i][j][k]
					- dDsBetaMomentumFluxS
					- dVorticityBeta;

				dataTendenciesNode[RIx][iA][iB][k] =
					- dInvJacobian * (
						  dDaMassFluxAlpha
						+ dDbMassFluxBeta)
					- dDzMassFluxVertical;

				dataTendenciesNode[PIx][iA][iB][k] =
					- dInvJacobian * (
						  dDaPressureFluxAlpha
						+ dDbPressureFluxBeta)
					- dDzPressureFluxVertical;
			}
			}
			}

			// Calculate slow tendencies on interfaces
			// the vertical flux of horizontal momentum.
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 1; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				const double dInvJacobianREdge =
					1.0 / dJacobianREdge[iA][iB][k];

				// Derivatives of vertical momentum flux
				double dDaVerticalMomentumFluxAlpha = 0.0;
				double dDbVerticalMomentumFluxBeta = 0.0;

				// Alpha derivative
				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDaVerticalMomentumFluxAlpha -=
						m_dAlphaVerticalMomentumFluxREdge[s][j][k]
						* dStiffness1D[i][s];
				}

				// Beta derivative
				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDbVerticalMomentumFluxBeta -=
						m_dBetaVerticalMomentumFluxREdge[i][s][k]
						* dStiffness1D[j][s];
				}

				dDaVerticalMomentumFluxAlpha *= dInvElementDeltaA;
				dDbVerticalMomentumFluxBeta  *= dInvElementDeltaB;

				const double dInvDeltaZ =
					1.0 / (dataZn[iA][iB][k] - dataZn[iA][iB][k-1]);

				const double dDzVerticalMomentumFluxW =
					dInvDeltaZ
					* (m_dSDotWNode[i][j][k] - m_dSDotWNode[i][j][k-1]);

				const double dDzPressure =
					dInvDeltaZ
					* (dataPressure[iA][iB][k] - dataPressure[iA][iB][k-1]);

				dataTendenciesREdge[WIx][iA][iB][k] =
					- dDzPressure
					- dataInitialREdge[RIx][iA][iB][k] * phys.GetG()
					- dInvJacobianREdge * (
						  dDaVerticalMomentumFluxAlpha
						+ dDbVerticalMomentumFluxBeta)
					- dDzVerticalMomentumFluxW;
			}
			}
			}
		}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::PerformAcousticLoop(
	int iDataInitial,
	int iDataTendencies,
	int iDataAcoustic0,
	int iDataAcoustic1,
	int iDataAcoustic2,
	double dDeltaT,
	bool fFinalLoop
) {

	// Start the function timer
	FunctionTimer timer("AcousticLoop");

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical elements
	const int nRElements = pGrid->GetRElements();

	// Physical constants
	const PhysicalConstants & phys = m_model.GetPhysicalConstants();

	// Indices of EquationSet variables
	const int UIx = 0;
	const int VIx = 1;
	const int PIx = 2;
	const int WIx = 3;
	const int RIx = 4;

	// Update horizontal velocities
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		// Metric terms
		const DataArray3D<double> & dContraMetric2DA =
			pPatch->GetContraMetric2DA();
		const DataArray3D<double> & dContraMetric2DB =
			pPatch->GetContraMetric2DB();

		const DataArray4D<double> & dDerivRNode =
			pPatch->GetDerivRNode();
		const DataArray4D<double> & dDerivRREdge =
			pPatch->GetDerivRREdge();

		const DataArray3D<double> & dataZn =
			pPatch->GetZLevels();
		const DataArray3D<double> & dataZi =
			pPatch->GetZInterfaces();

		// Data
		const DataArray4D<double> & dataInitialNode =
			pPatch->GetDataState(iDataInitial, DataLocation_Node);

		const DataArray4D<double> & dataInitialREdge =
			pPatch->GetDataState(iDataInitial, DataLocation_REdge);

		const DataArray4D<double> & dataTendenciesNode =
			pPatch->GetDataState(iDataTendencies, DataLocation_Node);

		const DataArray4D<double> & dataTendenciesREdge =
			pPatch->GetDataState(iDataTendencies, DataLocation_REdge);

		DataArray4D<double> & dataAcoustic0Node =
			pPatch->GetDataState(iDataAcoustic0, DataLocation_Node);

		DataArray4D<double> & dataAcoustic1Node =
			pPatch->GetDataState(iDataAcoustic1, DataLocation_Node);

		DataArray4D<double> & dataAcoustic2Node =
			pPatch->GetDataState(iDataAcoustic2, DataLocation_Node);

		const DataArray3D<double> & dataPressure =
			pPatch->GetDataPressure();

		// Element grid spacing and derivative coefficients
		const double dElementDeltaA = pPatch->GetElementDeltaA();
		const double dElementDeltaB = pPatch->GetElementDeltaB();

		const double dInvElementDeltaA = 1.0 / dElementDeltaA;
		const double dInvElementDeltaB = 1.0 / dElementDeltaB;

		const DataArray2D<double> & dDxBasis1D = pGrid->GetDxBasis1D();
		const DataArray2D<double> & dStiffness1D = pGrid->GetStiffness1D();

		// Get number of finite elements in each coordinate direction
		const int nElementCountA = pPatch->GetElementCountA();
		const int nElementCountB = pPatch->GetElementCountB();

		// Loop over all elements
		for (int a = 0; a < nElementCountA; a++) {
		for (int b = 0; b < nElementCountB; b++) {

			const int iElementA = a * m_nHorizontalOrder + box.GetHaloElements();
			const int iElementB = b * m_nHorizontalOrder + box.GetHaloElements();

			// Calculate updated acoustic pressure
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				m_dAcousticPressureTerm[i][j][k] =
					dataPressure[iA][iB][k]
					* ((1.0 + m_dBd) * dataAcoustic1Node[PIx][iA][iB][k]
						- m_dBd * dataAcoustic0Node[PIx][iA][iB][k]);
			}
			}
			}

			// Apply pressure gradient force to horizontal momentum
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				// Horizontal derivatives of the acoustic pressure field
				double dDaP = 0.0;
				double dDbP = 0.0;

				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDaP +=
						m_dAcousticPressureTerm[s][j][k]
						* dDxBasis1D[s][i];

					dDbP +=
						m_dAcousticPressureTerm[i][s][k]
						* dDxBasis1D[s][j];
				}

				dDaP *= dInvElementDeltaA;
				dDbP *= dInvElementDeltaB;

				// Vertical derivative of the acoustic pressure field
				double dDzP = 0.0;
				if (k == 0) {
					dDzP = (m_dAcousticPressureTerm[i][j][k+1]
						- m_dAcousticPressureTerm[i][j][k])
						/ (dataZn[iA][iB][k+1] - dataZn[iA][iB][k]);

				} else if (k == nRElements-1) {
					dDzP = (m_dAcousticPressureTerm[i][j][k]
						- m_dAcousticPressureTerm[i][j][k-1])
						/ (dataZn[iA][iB][k] - dataZn[iA][iB][k-1]);

				} else {
					dDzP = (m_dAcousticPressureTerm[i][j][k+1]
						- m_dAcousticPressureTerm[i][j][k-1])
						/ (dataZn[iA][iB][k+1] - dataZn[iA][iB][k-1]);
				}

				// Convert derivatives along s surfaces to z surfaces
				dDaP -= dDerivRNode[iA][iB][k][0] * dDzP;
				dDbP -= dDerivRNode[iA][iB][k][1] * dDzP;

				// Convert from covariant derivatives to contravariant
				const double dConDaP =
					  dContraMetric2DA[iA][iB][0] * dDaP
					+ dContraMetric2DA[iA][iB][1] * dDbP;

				const double dConDbP =
					  dContraMetric2DB[iA][iB][0] * dDaP
					+ dContraMetric2DB[iA][iB][1] * dDbP;

				// Update the horizontal momentum
				dataAcoustic2Node[UIx][iA][iB][k] =
					dataAcoustic1Node[UIx][iA][iB][k]
					- dDeltaT * dConDaP
					+ dDeltaT * dataTendenciesNode[UIx][iA][iB][k];

				dataAcoustic2Node[VIx][iA][iB][k] =
					dataAcoustic1Node[VIx][iA][iB][k]
					- dDeltaT * dConDbP
					+ dDeltaT * dataTendenciesNode[VIx][iA][iB][k];

				printf("%1.10e %1.10e\n",
					dConDaP, dataTendenciesNode[UIx][iA][iB][k]);
			}
			}
			}
		}
		}
	}

	// Apply direct stiffness summation
	pGrid->ApplyDSS(iDataAcoustic2, DataType_State);

	// Compute remaining update
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		// Metric terms
		const DataArray3D<double> & dJacobian =
			pPatch->GetJacobian();
		const DataArray3D<double> & dJacobianREdge =
			pPatch->GetJacobianREdge();

		const DataArray4D<double> & dDerivRNode =
			pPatch->GetDerivRNode();
		const DataArray4D<double> & dDerivRREdge =
			pPatch->GetDerivRREdge();

		const DataArray3D<double> & dataZn =
			pPatch->GetZLevels();
		const DataArray3D<double> & dataZi =
			pPatch->GetZInterfaces();

		// Altitude on levels and interfaces
		const DataArray3D<double> & dataPressure =
			pPatch->GetDataPressure();

		const DataArray4D<double> & dataInitialNode =
			pPatch->GetDataState(iDataInitial, DataLocation_Node);

		const DataArray4D<double> & dataInitialREdge =
			pPatch->GetDataState(iDataInitial, DataLocation_REdge);

		const DataArray4D<double> & dataTendenciesNode =
			pPatch->GetDataState(iDataTendencies, DataLocation_Node);

		const DataArray4D<double> & dataTendenciesREdge =
			pPatch->GetDataState(iDataTendencies, DataLocation_REdge);

		DataArray4D<double> & dataAcoustic1Node =
			pPatch->GetDataState(iDataAcoustic1, DataLocation_Node);

		DataArray4D<double> & dataAcoustic1REdge =
			pPatch->GetDataState(iDataAcoustic1, DataLocation_REdge);

		DataArray4D<double> & dataAcoustic2Node =
			pPatch->GetDataState(iDataAcoustic2, DataLocation_Node);

		DataArray4D<double> & dataAcoustic2REdge =
			pPatch->GetDataState(iDataAcoustic2, DataLocation_REdge);

		const DataArray2D<double> & dStiffness1D = pGrid->GetStiffness1D();

		// Get number of finite elements in each coordinate direction
		const int nElementCountA = pPatch->GetElementCountA();
		const int nElementCountB = pPatch->GetElementCountB();

		// Element grid spacing and derivative coefficients
		const double dElementDeltaA = pPatch->GetElementDeltaA();
		const double dElementDeltaB = pPatch->GetElementDeltaB();

		const double dInvElementDeltaA = 1.0 / dElementDeltaA;
		const double dInvElementDeltaB = 1.0 / dElementDeltaB;

		// Loop over all elements
		for (int a = 0; a < nElementCountA; a++) {
		for (int b = 0; b < nElementCountB; b++) {

			const int iElementA = a * m_nHorizontalOrder + box.GetHaloElements();
			const int iElementB = b * m_nHorizontalOrder + box.GetHaloElements();

			// Compute horizontal fluxes of rho and rhotheta
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				// Inverse density
				double dInvRhoInitial = 1.0 / dataInitialNode[RIx][iA][iB][k];

				// Alpha mass flux
				m_dAlphaMassFlux[i][j][k] =
					dJacobian[iA][iB][k]
					* dataAcoustic2Node[UIx][iA][iB][k];

				// Beta mass flux
				m_dBetaMassFlux[i][j][k] =
					dJacobian[iA][iB][k]
					* dataAcoustic2Node[VIx][iA][iB][k];

				// Alpha pressure flux
				m_dAlphaPressureFlux[i][j][k] =
					dJacobian[iA][iB][k]
					* dInvRhoInitial
					* dataAcoustic2Node[UIx][iA][iB][k]
					* dataInitialNode[PIx][iA][iB][k];

				// Beta mass flux
				m_dBetaPressureFlux[i][j][k] =
					dJacobian[iA][iB][k]
					* dInvRhoInitial
					* dataAcoustic2Node[VIx][iA][iB][k]
					* dataInitialNode[PIx][iA][iB][k];
			}
			}
			}

			// Compute vertical fluxes of rho and rhotheta
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 1; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				m_dZMassFluxREdge1[i][j][k] =
					dataAcoustic1REdge[WIx][iA][iB][k]
					- dDerivRREdge[iA][iB][k][0] * 0.5 * (
						dataAcoustic1Node[UIx][iA][iB][k-1]
						+ dataAcoustic1Node[UIx][iA][iB][k])
					- dDerivRREdge[iA][iB][k][1] * 0.5 * (
						dataAcoustic1Node[VIx][iA][iB][k-1]
						+ dataAcoustic1Node[VIx][iA][iB][k]);

				// Note that we have stored theta in dataInitialREdge[PIx]
				m_dZPressureFluxREdge1[i][j][k] =
					m_dZMassFluxREdge1[i][j][k]
					* dataInitialREdge[PIx][i][j][k];

				m_dZMassFluxREdge2[i][j][k] =
					- dDerivRREdge[iA][iB][k][0] * 0.5 * (
						dataAcoustic1Node[UIx][iA][iB][k-1]
						+ dataAcoustic1Node[UIx][iA][iB][k])
					- dDerivRREdge[iA][iB][k][1] * 0.5 * (
						dataAcoustic1Node[VIx][iA][iB][k-1]
						+ dataAcoustic1Node[VIx][iA][iB][k]);

				// Note that we have stored theta in dataInitialREdge[PIx]
				m_dZPressureFluxREdge2[i][j][k] =
					m_dZMassFluxREdge2[i][j][k]
					* dataInitialREdge[PIx][i][j][k];
			}
			}
			}

			// Compute forward and forward/backward updates to rho and rhotheta
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				const double dInvJacobian =
					1.0 / dJacobian[iA][iB][k];

				const double dInvDeltaZn =
					1.0 / (dataZi[iA][iB][k+1] - dataZi[iA][iB][k]);

				double dDaMassFlux = 0.0;
				double dDbMassFlux = 0.0;

				double dDaPressureFlux = 0.0;
				double dDbPressureFlux = 0.0;

				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDaMassFlux -=
						m_dAlphaMassFlux[s][j][k]
						* dStiffness1D[i][s];

					dDaPressureFlux -=
						m_dAlphaPressureFlux[s][j][k]
						* dStiffness1D[i][s];
				}

				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDaMassFlux -=
						m_dBetaMassFlux[i][s][k]
						* dStiffness1D[j][s];

					dDaPressureFlux -=
						m_dBetaPressureFlux[i][s][k]
						* dStiffness1D[j][s];
				}

				dDaMassFlux *= dInvElementDeltaA * dInvJacobian;
				dDbMassFlux *= dInvElementDeltaB * dInvJacobian;

				dDaPressureFlux *= dInvElementDeltaA * dInvJacobian;
				dDbPressureFlux *= dInvElementDeltaB * dInvJacobian;

				const double dDzMassFlux1 = dInvDeltaZn
					* (m_dZMassFluxREdge1[i][j][k+1]
					- m_dZMassFluxREdge1[i][j][k]);

				const double dDzMassFlux2 = dInvDeltaZn
					* (m_dZMassFluxREdge2[i][j][k+1]
					- m_dZMassFluxREdge2[i][j][k]);

				const double dDzPressureFlux1 = dInvDeltaZn
					* (m_dZPressureFluxREdge1[i][j][k+1]
					- m_dZPressureFluxREdge1[i][j][k]);

				const double dDzPressureFlux2 = dInvDeltaZn
					* (m_dZPressureFluxREdge2[i][j][k+1]
					- m_dZPressureFluxREdge2[i][j][k]);

				m_dNodalMassUpdate[i][j][k] = - dDeltaT * (
					  dDaMassFlux
					+ dDbMassFlux
					+ 0.5 * (1.0 - m_dBs) * dDzMassFlux1
					+ 0.5 * (1.0 + m_dBs) * dDzMassFlux2
					- dataTendenciesNode[RIx][iA][iB][k]);

				m_dNodalPressureUpdate[i][j][k] = - dDeltaT * (
					  dDaPressureFlux
					+ dDbPressureFlux
					+ 0.5 * (1.0 - m_dBs) * dDzPressureFlux1
					+ 0.5 * (1.0 + m_dBs) * dDzPressureFlux2
					- dataTendenciesNode[PIx][iA][iB][k]);
			}
			}
			}

			// Timescale terms
			const double dTimeScale = dDeltaT * 0.5 * (1.0 + m_dBs);
			const double dTimeScale2 = dTimeScale * dTimeScale;

			// Fill in vertical column arrays
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
				m_dB[i][j][0] = 1.0;
				m_dB[i][j][nRElements] = 1.0;
			}
			}

			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 1; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				const double dInvDeltaZk   = 1.0 / (dataZi[iA][iB][k+1] - dataZi[iA][iB][k]);
				const double dInvDeltaZkm  = 1.0 / (dataZi[iA][iB][k] - dataZi[iA][iB][k-1]);
				const double dInvDeltaZhat = 1.0 / (dataZn[iA][iB][k] - dataZn[iA][iB][k-1]);

				// Note that we have stored theta in dataInitialREdge[PIx]
				m_dA[i][j][k] = - dTimeScale2 * dInvDeltaZkm * (
					dInvDeltaZhat
						* dataPressure[iA][iB][k-1]
						* dataInitialREdge[PIx][iA][iB][k-1]
					- 0.5 * phys.GetG());

				m_dB[i][j][k] = 1.0 + dTimeScale2 * (
					dataInitialREdge[PIx][iA][iB][k]
						* (dataPressure[iA][iB][k] * dInvDeltaZk
							+ dataPressure[iA][iB][k-1] * dInvDeltaZkm)
					+ 0.5 * phys.GetG()
						* (dInvDeltaZk - dInvDeltaZkm));

				m_dC[i][j][k] = - dTimeScale2 * dInvDeltaZk * (
					+ dInvDeltaZhat
						* dataPressure[iA][iB][k]
						* dataInitialREdge[PIx][iA][iB][k+1]
					+ 0.5 * phys.GetG());

				const double dDzPressure = dInvDeltaZhat
					* (dataPressure[iA][iB][k] * dataAcoustic1Node[PIx][iA][iB][k]
					- dataPressure[iA][iB][k-1] * dataAcoustic1Node[PIx][iA][iB][k-1]);

				const double dDzPressureUpdate = dInvDeltaZhat
					* (m_dNodalPressureUpdate[i][j][k]
						- m_dNodalPressureUpdate[i][j][k-1]);

				const double dIntRho =
					0.5 * phys.GetG() * (
						dataAcoustic1Node[RIx][iA][iB][k]
						+ dataAcoustic1Node[RIx][iA][iB][k-1]);

				const double dIntRhoUpdate =
					0.5 * phys.GetG() * (
						m_dNodalMassUpdate[i][j][k]
						+ m_dNodalMassUpdate[i][j][k-1]);

				m_dD[i][j][k] =
					dataAcoustic1REdge[WIx][iA][iB][k]
					- dDeltaT * (
						dDzPressure
						+ dIntRho
						- dataTendenciesREdge[WIx][iA][iB][k])
					- dTimeScale * (
						dDzPressureUpdate
						+ dIntRhoUpdate);
			}
			}
			}

			// Perform a tridiagonal solve for dataAcoustic2REdge
			int nREdges = nRElements+1;
			int nRHS = 1;
			int nLDB = nREdges;

			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
/*
				for (int k = 0; k < nREdges; k++) {
					printf("%1.10e %1.10e %1.10e %1.10e\n",
						m_dA[i][j][k],
						m_dB[i][j][k],
						m_dC[i][j][k],
						m_dD[i][j][k]);
				}
				_EXCEPTION();
*/
#ifdef TEMPEST_LAPACK_ACML_INTERFACE
				dgtsv(
					nREdges,
					nRHS,
					&(m_dA[i][j][0]),
					&(m_dB[i][j][0]),
					&(m_dC[i][j][0]),
					&(m_dD[i][j][0]),
					nLDB,
					&(m_nInfo[i][j]));
#endif
#ifdef TEMPEST_LAPACK_ESSL_INTERFACE
				dgtsv(
					nREdges,
					nRHS,
					&(m_dA[i][j][0]),
					&(m_dB[i][j][0]),
					&(m_dC[i][j][0]),
					&(m_dD[i][j][0]),
					nLDB);
				m_nInfo[i][j] = 0;
#endif
#ifdef TEMPEST_LAPACK_FORTRAN_INTERFACE
				dgtsv_(
					&nREdges,
					&nRHS,
					&(m_dA[i][j][0]),
					&(m_dB[i][j][0]),
					&(m_dC[i][j][0]),
					&(m_dD[i][j][0]),
					&nLDB,
					&(m_nInfo[i][j]));
#endif
			}
			}

			// Check return values from tridiagonal solve
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
				if (m_nInfo[i][j] != 0) {
					_EXCEPTION1("Failure in tridiagonal solve: %i",
						m_nInfo[i][j]);
				}
			}
			}
/*
			// Perform update to mass and potential temperature
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {
			for (int k = 0; k < nRElements; k++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				const double dInvDeltaZn =
					1.0 / (dataZi[iA][iB][k+1] - dataZi[iA][iB][k]);

				// Store updated vertical momentum
				dataAcoustic2REdge[WIx][i][j][k] = m_dD[i][j][k];

				// Updated vertical mass flux
				const double dDzMassFlux =
					dInvDeltaZn * (m_dD[i][j][k+1] - m_dD[i][j][k]);

				// Store updated density
				dataAcoustic2Node[RIx][i][j][k] =
					dataAcoustic1Node[RIx][i][j][k]
					+ dDeltaT * (
						m_dNodalMassUpdate[i][j][k]
						- 0.5 * (1.0 + m_dBs) * dDzMassFlux);

				const double dDzPressureFlux = dInvDeltaZn
					* (dataAcoustic2REdge[WIx][iA][iB][k+1]
					 	* dataInitialREdge[PIx][iA][iB][k+1]
					- dataAcoustic2REdge[WIx][iA][iB][k]
						* dataInitialREdge[PIx][iA][iB][k]);

				// Store updated potential temperature density
				dataAcoustic2Node[PIx][i][j][k] =
					dataAcoustic1Node[PIx][i][j][k]
					+ dDeltaT * (
						m_dNodalPressureUpdate[i][j][k]
						- 0.5 * (1.0 + m_dBs) * dDzPressureFlux);
			}
			}
			}
*/
			// Perform update to mass and potential temperature
			if (fFinalLoop) {
				for (int i = 0; i < m_nHorizontalOrder; i++) {
				for (int j = 0; j < m_nHorizontalOrder; j++) {
				for (int k = 0; k < nRElements; k++) {

					const int iA = iElementA + i;
					const int iB = iElementB + j;

					dataAcoustic2Node[UIx][iA][iB][k] +=
						dataInitialNode[UIx][iA][iB][k];
					dataAcoustic2Node[VIx][iA][iB][k] +=
						dataInitialNode[VIx][iA][iB][k];
					dataAcoustic2Node[PIx][iA][iB][k] +=
						dataInitialNode[PIx][iA][iB][k];
					dataAcoustic2Node[RIx][iA][iB][k] +=
						dataInitialNode[RIx][iA][iB][k];
					dataAcoustic2REdge[WIx][iA][iB][k] +=
						dataInitialREdge[WIx][iA][iB][k];
				}
				}
				}
			}
		}
		}
	}

	// Apply direct stiffness summation
	if (fFinalLoop) {
		pGrid->ApplyDSS(iDataAcoustic2, DataType_State);
	}
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::StepExplicit(
	int iDataInitial,
	int iDataUpdate,
	const Time & time,
	double dDeltaT
) {
	if (iDataInitial == iDataUpdate) {
		_EXCEPTIONT(
			"HorizontalDynamics Step must have iDataInitial != iDataUpdate");
	}

	// Start the function timer
	FunctionTimer timer("HorizontalStepNonhydrostaticPrimitive");

	// Indices of EquationSet variables
	const int UIx = 0;
	const int VIx = 1;
	const int PIx = 2;
	const int WIx = 3;
	const int RIx = 4;

	// Check formulation
#ifndef FORMULATION_RHOTHETA_PI
	_EXCEPTIONT("Only FORMULATION_RHOTHETA_PI is supported");
#endif

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical elements
	const int nRElements = pGrid->GetRElements();

	// Equation set
	const EquationSet & eqn = m_model.GetEquationSet();

	if (eqn.GetType() != EquationSet::PrimitiveNonhydrostaticEquations) {
		_EXCEPTIONT("Only EquationSet::PrimitiveNonhydrostaticEquations supported");
	}

	// Check variable locations
	if ((pGrid->GetVarLocation(UIx) != DataLocation_Node) ||
	    (pGrid->GetVarLocation(VIx) != DataLocation_Node) ||
	    (pGrid->GetVarLocation(PIx) != DataLocation_Node) ||
	    (pGrid->GetVarLocation(WIx) != DataLocation_REdge) ||
	    (pGrid->GetVarLocation(RIx) != DataLocation_Node)
	) {
		_EXCEPTIONT("Only Lorenz staggering is supported");
	}

	// Physical constants
	const PhysicalConstants & phys = m_model.GetPhysicalConstants();

	// First data instance reserved for this class
	const int iDataTendenciesIx = m_model.GetFirstHorizontalDynamicsDataInstance();
	const int iDataAcousticIndex1 = iDataTendenciesIx+1;
	const int iDataAcousticIndex2 = iDataTendenciesIx+2;

	// Pre-calculate pressure on model levels
	{
		for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
			GridPatchGLL * pPatch =
				dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

			const PatchBox & box = pPatch->GetPatchBox();

			DataArray3D<double> & dataPressure =
				pPatch->GetDataPressure();

			const DataArray4D<double> & dataInitialNode =
				pPatch->GetDataState(iDataInitial, DataLocation_Node);

			// Calculate diagnostic pressure slope and store in dataPressure
			for (int i = box.GetAInteriorBegin(); i < box.GetAInteriorEnd(); i++) {
			for (int j = box.GetBInteriorBegin(); j < box.GetBInteriorEnd(); j++) {
			for (int k = 0; k < nRElements; k++) {
				dataPressure[i][j][k] =
					phys.PressureFromRhoTheta(dataInitialNode[PIx][i][j][k]);
			}
			}
			}
		}
	}

	// Calculate the slow tendencies in the momentum variables
	CalculateTendencies(iDataInitial, iDataTendenciesIx, dDeltaT);

	// Pre-calculate diagnostic pressure tendency on model levels
	// (needed in acoustic loop calculations)
	{
		for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
			GridPatchGLL * pPatch =
				dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

			const PatchBox & box = pPatch->GetPatchBox();

			DataArray3D<double> & dataPressure =
				pPatch->GetDataPressure();

			const DataArray4D<double> & dataInitialNode =
				pPatch->GetDataState(iDataInitial, DataLocation_Node);

			const DataArray4D<double> & dataTendenciesNode =
				pPatch->GetDataState(iDataTendenciesIx, DataLocation_Node);

			DataArray4D<double> & dataUpdateNode =
				pPatch->GetDataState(iDataUpdate, DataLocation_Node);

			for (int i = box.GetAInteriorBegin(); i < box.GetAInteriorEnd(); i++) {
			for (int j = box.GetBInteriorBegin(); j < box.GetBInteriorEnd(); j++) {
			for (int k = 0; k < nRElements; k++) {
				dataUpdateNode[UIx][i][j][k] =
					dataInitialNode[UIx][i][j][k]
					+ dDeltaT * dataTendenciesNode[UIx][i][j][k];
				dataUpdateNode[VIx][i][j][k] =
					dataInitialNode[VIx][i][j][k]
					+ dDeltaT * dataTendenciesNode[VIx][i][j][k];
				dataUpdateNode[RIx][i][j][k] =
					dataInitialNode[RIx][i][j][k]
					+ dDeltaT * dataTendenciesNode[RIx][i][j][k];
				dataUpdateNode[PIx][i][j][k] =
					dataInitialNode[PIx][i][j][k]
					+ dDeltaT * dataTendenciesNode[PIx][i][j][k];
			}
			}
			}
		}
	}
	
	return;

	// Pre-calculate diagnostic pressure tendency on model levels
	// (needed in acoustic loop calculations)
	{
		for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
			GridPatchGLL * pPatch =
				dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

			const PatchBox & box = pPatch->GetPatchBox();

			DataArray3D<double> & dataPressure =
				pPatch->GetDataPressure();

			const DataArray4D<double> & dataInitialNode =
				pPatch->GetDataState(iDataInitial, DataLocation_Node);

			for (int i = box.GetAInteriorBegin(); i < box.GetAInteriorEnd(); i++) {
			for (int j = box.GetBInteriorBegin(); j < box.GetBInteriorEnd(); j++) {
			for (int k = 0; k < nRElements; k++) {
				dataPressure[i][j][k] *=
					phys.GetGamma() / dataInitialNode[PIx][i][j][k];
			}
			}
			}
		}
	}

	{
		pGrid->ZeroData(iDataUpdate, DataType_State);
		pGrid->ZeroData(iDataAcousticIndex1, DataType_State);
		pGrid->ZeroData(iDataAcousticIndex2, DataType_State);

		PerformAcousticLoop(
			iDataInitial,
			iDataTendenciesIx,
			iDataAcousticIndex1,
			iDataAcousticIndex2,
			iDataUpdate,
			dDeltaT,
			true);
	}

	// Perform three sub-cycles of the acoustic loop
	{
		pGrid->ZeroData(iDataUpdate, DataType_State);
		pGrid->ZeroData(iDataAcousticIndex1, DataType_State);
		pGrid->ZeroData(iDataAcousticIndex2, DataType_State);

		PerformAcousticLoop(
			iDataInitial,
			iDataTendenciesIx,
			iDataAcousticIndex2,
			iDataUpdate,
			iDataAcousticIndex1,
			dDeltaT / 3.0,
			false);

		PerformAcousticLoop(
			iDataInitial,
			iDataTendenciesIx,
			iDataUpdate,
			iDataAcousticIndex1,
			iDataAcousticIndex2,
			dDeltaT / 3.0,
			false);

		PerformAcousticLoop(
			iDataInitial,
			iDataTendenciesIx,
			iDataAcousticIndex1,
			iDataAcousticIndex2,
			iDataUpdate,
			dDeltaT / 3.0,
			true);
	}
	
	return;

	// Apply positive definite filter to tracers
	//FilterNegativeTracers(iDataUpdate);

/*
	// Uniform diffusion of U and V with vector diffusion coeff
	if (pGrid->HasUniformDiffusion()) {
		ApplyVectorHyperdiffusion(
			iDataInitial,
			iDataUpdate,
			dDeltaT,
			- pGrid->GetVectorUniformDiffusionCoeff(),
			- pGrid->GetVectorUniformDiffusionCoeff(),
			false);

		ApplyVectorHyperdiffusion(
			DATA_INDEX_REFERENCE,
			iDataUpdate,
			dDeltaT,
			pGrid->GetVectorUniformDiffusionCoeff(),
			pGrid->GetVectorUniformDiffusionCoeff(),
			false);

		if (eqn.GetType() == EquationSet::PrimitiveNonhydrostaticEquations) {

			// Uniform diffusion of Theta with scalar diffusion coeff
			ApplyScalarHyperdiffusion(
				iDataInitial,
				iDataUpdate,
				dDeltaT,
				pGrid->GetScalarUniformDiffusionCoeff(),
				false,
				2,
				true);

			// Uniform diffusion of W with vector diffusion coeff
			ApplyScalarHyperdiffusion(
				iDataInitial,
				iDataUpdate,
				dDeltaT,
				pGrid->GetVectorUniformDiffusionCoeff(),
				false,
				3,
				true);
		}
	}
*/
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::ApplyScalarHyperdiffusion(
	int iDataInitial,
	int iDataUpdate,
	double dDeltaT,
	double dNu,
	bool fScaleNuLocally,
	int iComponent,
	bool fRemoveRefState
) {
	// Indices of EquationSet variables
	const int UIx = 0;
	const int VIx = 1;
	const int PIx = 2;
	const int WIx = 3;
	const int RIx = 4;

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of radial elements in grid
	const int nRElements = pGrid->GetRElements();

	// Check argument
	if (iComponent < (-1)) {
		_EXCEPTIONT("Invalid component index");
	}

	// Perform local update
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		const DataArray3D<double> & dElementAreaNode =
			pPatch->GetElementAreaNode();
		const DataArray3D<double> & dJacobianNode =
			pPatch->GetJacobian();
		const DataArray3D<double> & dJacobianREdge =
			pPatch->GetJacobianREdge();
		const DataArray3D<double> & dContraMetricA =
			pPatch->GetContraMetric2DA();
		const DataArray3D<double> & dContraMetricB =
			pPatch->GetContraMetric2DB();

		// Grid data
		DataArray4D<double> & dataInitialNode =
			pPatch->GetDataState(iDataInitial, DataLocation_Node);

		DataArray4D<double> & dataInitialREdge =
			pPatch->GetDataState(iDataInitial, DataLocation_REdge);

		DataArray4D<double> & dataUpdateNode =
			pPatch->GetDataState(iDataUpdate, DataLocation_Node);

		DataArray4D<double> & dataUpdateREdge =
			pPatch->GetDataState(iDataUpdate, DataLocation_REdge);

		const DataArray4D<double> & dataRefNode =
			pPatch->GetReferenceState(DataLocation_Node);

		const DataArray4D<double> & dataRefREdge =
			pPatch->GetReferenceState(DataLocation_REdge);

		// Tracer data
		DataArray4D<double> & dataInitialTracer =
			pPatch->GetDataTracers(iDataInitial);

		DataArray4D<double> & dataUpdateTracer =
			pPatch->GetDataTracers(iDataUpdate);

		// Element grid spacing and derivative coefficients
		const double dElementDeltaA = pPatch->GetElementDeltaA();
		const double dElementDeltaB = pPatch->GetElementDeltaB();

		const double dInvElementDeltaA = 1.0 / dElementDeltaA;
		const double dInvElementDeltaB = 1.0 / dElementDeltaB;

		const DataArray2D<double> & dDxBasis1D = pGrid->GetDxBasis1D();
		const DataArray2D<double> & dStiffness1D = pGrid->GetStiffness1D();

		// Number of finite elements
		int nElementCountA = pPatch->GetElementCountA();
		int nElementCountB = pPatch->GetElementCountB();

		// Compute new hyperviscosity coefficient
		double dLocalNu = dNu;

		if (fScaleNuLocally) {
			double dReferenceLength = pGrid->GetReferenceLength();
			if (dReferenceLength != 0.0) {
				dLocalNu *= pow(dElementDeltaA / dReferenceLength, 3.2);
			}
		}

		// State variables and tracers
		for (int iType = 0; iType < 2; iType++) {

			int nComponentStart;
			int nComponentEnd;

			if (iType == 0) {
				nComponentStart = 2;
				nComponentEnd = m_model.GetEquationSet().GetComponents();

				if (iComponent != (-1)) {
					if (iComponent >= nComponentEnd) {
						_EXCEPTIONT("Invalid component index");
					}

					nComponentStart = iComponent;
					nComponentEnd = iComponent+1;
				}

			} else {
				nComponentStart = 0;
				nComponentEnd = m_model.GetEquationSet().GetTracers();

				if (iComponent != (-1)) {
					continue;
				}
			}

			// Loop over all components
			for (int c = nComponentStart; c < nComponentEnd; c++) {

				int nElementCountR;

				const DataArray4D<double> * pDataInitial;
				DataArray4D<double> * pDataUpdate;
				const DataArray4D<double> * pDataRef;
				const DataArray3D<double> * pJacobian;

				if (iType == 0) {
					if (pGrid->GetVarLocation(c) == DataLocation_Node) {
						pDataInitial = &dataInitialNode;
						pDataUpdate  = &dataUpdateNode;
						pDataRef = &dataRefNode;
						nElementCountR = nRElements;
						pJacobian = &dJacobianNode;

					} else if (pGrid->GetVarLocation(c) == DataLocation_REdge) {
						pDataInitial = &dataInitialREdge;
						pDataUpdate  = &dataUpdateREdge;
						pDataRef = &dataRefREdge;
						nElementCountR = nRElements + 1;
						pJacobian = &dJacobianREdge;

					} else {
						_EXCEPTIONT("UNIMPLEMENTED");
					}

				} else {
					pDataInitial = &dataInitialTracer;
					pDataUpdate = &dataUpdateTracer;
					nElementCountR = nRElements;
					pJacobian = &dJacobianNode;
				}

				// Loop over all finite elements
				for (int a = 0; a < nElementCountA; a++) {
				for (int b = 0; b < nElementCountB; b++) {
				for (int k = 0; k < nElementCountR; k++) {

					int iElementA =
						a * m_nHorizontalOrder + box.GetHaloElements();
					int iElementB =
						b * m_nHorizontalOrder + box.GetHaloElements();

					// Store the buffer state
					for (int i = 0; i < m_nHorizontalOrder; i++) {
					for (int j = 0; j < m_nHorizontalOrder; j++) {
						int iA = iElementA + i;
						int iB = iElementB + j;

						m_dBufferState[i][j] = (*pDataInitial)[c][iA][iB][k];
					}
					}

					// Remove the reference state from the buffer state
					if (fRemoveRefState) {
						for (int i = 0; i < m_nHorizontalOrder; i++) {
						for (int j = 0; j < m_nHorizontalOrder; j++) {
							int iA = iElementA + i;
							int iB = iElementB + j;

							m_dBufferState[i][j] -=
								(*pDataRef)[c][iA][iB][k];
						}
						}
					}

					// Calculate the pointwise gradient of the scalar field
					for (int i = 0; i < m_nHorizontalOrder; i++) {
					for (int j = 0; j < m_nHorizontalOrder; j++) {
						int iA = iElementA + i;
						int iB = iElementB + j;

						double dDaPsi = 0.0;
						double dDbPsi = 0.0;
						for (int s = 0; s < m_nHorizontalOrder; s++) {
							dDaPsi +=
								m_dBufferState[s][j]
								* dDxBasis1D[s][i];

							dDbPsi +=
								m_dBufferState[i][s]
								* dDxBasis1D[s][j];
						}

						dDaPsi *= dInvElementDeltaA;
						dDbPsi *= dInvElementDeltaB;

						m_dJGradientA[i][j] = (*pJacobian)[iA][iB][k] * (
							+ dContraMetricA[iA][iB][0] * dDaPsi
							+ dContraMetricA[iA][iB][1] * dDbPsi);

						m_dJGradientB[i][j] = (*pJacobian)[iA][iB][k] * (
							+ dContraMetricB[iA][iB][0] * dDaPsi
							+ dContraMetricB[iA][iB][1] * dDbPsi);
					}
					}

					// Pointwise updates
#ifdef FIX_ELEMENT_MASS_NONHYDRO
					double dElementMassFluxA = 0.0;
					double dElementMassFluxB = 0.0;
					double dMassFluxPerNodeA = 0.0;
					double dMassFluxPerNodeB = 0.0;
					double dElTotalArea = 0.0;
#endif

					for (int i = 0; i < m_nHorizontalOrder; i++) {
					for (int j = 0; j < m_nHorizontalOrder; j++) {
						int iA = iElementA + i;
						int iB = iElementB + j;

						// Inverse Jacobian and Jacobian
						const double dInvJacobian = 1.0 / (*pJacobian)[iA][iB][k];

						// Compute integral term
						double dUpdateA = 0.0;
						double dUpdateB = 0.0;

						for (int s = 0; s < m_nHorizontalOrder; s++) {
							dUpdateA +=
								m_dJGradientA[s][j]
								* dStiffness1D[i][s];

							dUpdateB +=
								m_dJGradientB[i][s]
								* dStiffness1D[j][s];
						}

						dUpdateA *= dInvElementDeltaA;
						dUpdateB *= dInvElementDeltaB;

#ifdef FIX_ELEMENT_MASS_NONHYDRO
							if (c == RIx) {
		 						double dInvJacobian = 1.0 / (*pJacobian)[iA][iB][k];
								// Integrate element mass fluxes
								dElementMassFluxA += dInvJacobian *
									dUpdateA * dElementAreaNode[iA][iB][k];
								dElementMassFluxB += dInvJacobian *
									dUpdateB * dElementAreaNode[iA][iB][k];
								dElTotalArea += dElementAreaNode[iA][iB][k];

								// Store the local element fluxes
								m_dAlphaElMassFlux[i][j] = dUpdateA;
								m_dBetaElMassFlux[i][j] = dUpdateB;
							} else {
								// Apply update
								(*pDataUpdate)[c][iA][iB][k] -=
									dDeltaT * dInvJacobian * dLocalNu
										* (dUpdateA + dUpdateB);
							}
#else
						// Apply update
						(*pDataUpdate)[c][iA][iB][k] -=
							dDeltaT * dInvJacobian * dLocalNu
								* (dUpdateA + dUpdateB);
#endif
					}
					}
#ifdef FIX_ELEMENT_MASS_NONHYDRO
					if (c == RIx) {
						double dMassFluxPerNodeA = dElementMassFluxA / dElTotalArea;
						double dMassFluxPerNodeB = dElementMassFluxB / dElTotalArea;

						// Compute the fixed mass update
						for (int i = 0; i < m_nHorizontalOrder; i++) {
						for (int j = 0; j < m_nHorizontalOrder; j++) {

							int iA = iElementA + i;
							int iB = iElementB + j;

							// Inverse Jacobian
							const double dInvJacobian = 1.0 / (*pJacobian)[iA][iB][k];
							const double dJacobian = (*pJacobian)[iA][iB][k];

							m_dAlphaElMassFlux[i][j] -= dJacobian * dMassFluxPerNodeA;
							m_dBetaElMassFlux[i][j] -= dJacobian * dMassFluxPerNodeB;

							// Update fixed density on model levels
							(*pDataUpdate)[c][iA][iB][k] -=
								dDeltaT * dInvJacobian * dLocalNu *
									(m_dAlphaElMassFlux[i][j] + m_dBetaElMassFlux[i][j]);
						}
						}
					}
#endif
				}
				}
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::ApplyVectorHyperdiffusion(
	int iDataInitial,
	int iDataUpdate,
	double dDeltaT,
	double dNuDiv,
	double dNuVort,
	bool fScaleNuLocally
) {
	// Variable indices
	const int UIx = 0;
	const int VIx = 1;
	const int WIx = 3;

	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical elements
	const int nRElements = pGrid->GetRElements();

	// Apply viscosity to reference state
	bool fApplyToRefState = false;
	if (iDataInitial == DATA_INDEX_REFERENCE) {
		iDataInitial = 0;
		fApplyToRefState = true;
	}

	// Loop over all patches
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		const DataArray2D<double> & dJacobian2D =
			pPatch->GetJacobian2D();
		const DataArray3D<double> & dContraMetric2DA =
			pPatch->GetContraMetric2DA();
		const DataArray3D<double> & dContraMetric2DB =
			pPatch->GetContraMetric2DB();

		DataArray4D<double> & dataInitial =
			pPatch->GetDataState(iDataInitial, DataLocation_Node);

		DataArray4D<double> & dataUpdate =
			pPatch->GetDataState(iDataUpdate, DataLocation_Node);

		DataArray4D<double> & dataRef =
			pPatch->GetReferenceState(DataLocation_Node);

		// Element grid spacing and derivative coefficients
		const double dElementDeltaA = pPatch->GetElementDeltaA();
		const double dElementDeltaB = pPatch->GetElementDeltaB();

		const double dInvElementDeltaA = 1.0 / dElementDeltaA;
		const double dInvElementDeltaB = 1.0 / dElementDeltaB;

		const DataArray2D<double> & dDxBasis1D = pGrid->GetDxBasis1D();
		const DataArray2D<double> & dStiffness1D = pGrid->GetStiffness1D();

		// Compute curl and divergence of U on the grid
		DataArray3D<double> dataUa;
		dataUa.SetSize(
			dataInitial.GetSize(1),
			dataInitial.GetSize(2),
			dataInitial.GetSize(3));

		DataArray3D<double> dataUb;
		dataUb.SetSize(
			dataInitial.GetSize(1),
			dataInitial.GetSize(2),
			dataInitial.GetSize(3));

		if (fApplyToRefState) {
			dataUa.AttachToData(&(dataRef[UIx][0][0][0]));
			dataUb.AttachToData(&(dataRef[VIx][0][0][0]));
		} else {
			dataUa.AttachToData(&(dataInitial[UIx][0][0][0]));
			dataUb.AttachToData(&(dataInitial[VIx][0][0][0]));
		}

		// Compute curl and divergence of U on the grid
		pPatch->ComputeCurlAndDiv(dataUa, dataUb);

		// Get curl and divergence
		const DataArray3D<double> & dataCurl = pPatch->GetDataVorticity();
		const DataArray3D<double> & dataDiv  = pPatch->GetDataDivergence();

		// Compute new hyperviscosity coefficient
		double dLocalNuDiv  = dNuDiv;
		double dLocalNuVort = dNuVort;

		if (fScaleNuLocally) {
			double dReferenceLength = pGrid->GetReferenceLength();
			if (dReferenceLength != 0.0) {
				dLocalNuDiv =
					dLocalNuDiv  * pow(dElementDeltaA / dReferenceLength, 3.2);
				dLocalNuVort =
					dLocalNuVort * pow(dElementDeltaA / dReferenceLength, 3.2);
			}
		}

		// Number of finite elements
		int nElementCountA = pPatch->GetElementCountA();
		int nElementCountB = pPatch->GetElementCountB();

		// Loop over all finite elements
		for (int a = 0; a < nElementCountA; a++) {
		for (int b = 0; b < nElementCountB; b++) {
		for (int k = 0; k < nRElements; k++) {

			const int iElementA = a * m_nHorizontalOrder + box.GetHaloElements();
			const int iElementB = b * m_nHorizontalOrder + box.GetHaloElements();

			// Pointwise update of horizontal velocities
			for (int i = 0; i < m_nHorizontalOrder; i++) {
			for (int j = 0; j < m_nHorizontalOrder; j++) {

				const int iA = iElementA + i;
				const int iB = iElementB + j;

				// Compute hyperviscosity sums
				double dDaDiv = 0.0;
				double dDbDiv = 0.0;

				double dDaCurl = 0.0;
				double dDbCurl = 0.0;

				for (int s = 0; s < m_nHorizontalOrder; s++) {
					dDaDiv -=
						  dStiffness1D[i][s]
						* dataDiv[iElementA+s][iB][k];

					dDbDiv -=
						  dStiffness1D[j][s]
						* dataDiv[iA][iElementB+s][k];

					dDaCurl -=
						  dStiffness1D[i][s]
						* dataCurl[iElementA+s][iB][k];

					dDbCurl -=
						  dStiffness1D[j][s]
						* dataCurl[iA][iElementB+s][k];
				}

				dDaDiv *= dInvElementDeltaA;
				dDbDiv *= dInvElementDeltaB;

				dDaCurl *= dInvElementDeltaA;
				dDbCurl *= dInvElementDeltaB;

				// Apply update
				double dUpdateUa =
					+ dLocalNuDiv * dDaDiv
					- dLocalNuVort * dJacobian2D[iA][iB] * (
						  dContraMetric2DB[iA][iB][0] * dDaCurl
						+ dContraMetric2DB[iA][iB][1] * dDbCurl);

				double dUpdateUb =
					+ dLocalNuDiv * dDbDiv
					+ dLocalNuVort * dJacobian2D[iA][iB] * (
						  dContraMetric2DA[iA][iB][0] * dDaCurl
						+ dContraMetric2DA[iA][iB][1] * dDbCurl);

				dataUpdate[UIx][iA][iB][k] -= dDeltaT * dUpdateUa;

				if (pGrid->GetIsCartesianXZ() == false) {
					dataUpdate[VIx][iA][iB][k] -= dDeltaT * dUpdateUb;
				}
			}
			}
		}
		}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

//#pragma message "jeguerra: Clean up this function"

void SplitExplicitDynamics::ApplyRayleighFriction(
	int iDataUpdate,
	double dDeltaT
) {
	// Get a copy of the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Number of vertical levels
	const int nRElements = pGrid->GetRElements();

	// Number of components to hit with friction
	int nComponents = m_model.GetEquationSet().GetComponents();

	// Equation set being solved
	int nEqSet = m_model.GetEquationSet().GetType();

	bool fCartXZ = pGrid->GetIsCartesianXZ();

	int nEffectiveC[nComponents];
	// 3D primitive nonhydro models with no density treatment
	if ((nEqSet == EquationSet::PrimitiveNonhydrostaticEquations) && !fCartXZ) {
		nEffectiveC[0] = 0; nEffectiveC[1] = 1;
		nEffectiveC[2] = 2; nEffectiveC[3] = 3;
		nEffectiveC[nComponents - 1] = 0;
		nComponents = nComponents - 1;
	}
	// 2D Cartesian XZ primitive nonhydro models with no density treatment
	else if ((nEqSet == EquationSet::PrimitiveNonhydrostaticEquations) && fCartXZ) {
		nEffectiveC[0] = 0;
		nEffectiveC[1] = 2;
		nEffectiveC[2] = 3;
		nEffectiveC[nComponents - 2] = 0;
		nEffectiveC[nComponents - 1] = 0;
		nComponents = nComponents - 2;
	}
	// Other model types (advection, shallow water, mass coord)
	else {
		for (int nc = 0; nc < nComponents; nc++) {
			nEffectiveC[nc] = nc;
		}
	}

	// Subcycle the rayleigh update
	int nRayleighCycles = 10;
	double dRayleighFactor = 1.0 / nRayleighCycles;

	// Perform local update
	for (int n = 0; n < pGrid->GetActivePatchCount(); n++) {
		GridPatchGLL * pPatch =
			dynamic_cast<GridPatchGLL*>(pGrid->GetActivePatch(n));

		const PatchBox & box = pPatch->GetPatchBox();

		// Grid data
		DataArray4D<double> & dataUpdateNode =
			pPatch->GetDataState(iDataUpdate, DataLocation_Node);

		DataArray4D<double> & dataUpdateREdge =
			pPatch->GetDataState(iDataUpdate, DataLocation_REdge);

		// Reference state
		const DataArray4D<double> & dataReferenceNode =
			pPatch->GetReferenceState(DataLocation_Node);

		const DataArray4D<double> & dataReferenceREdge =
			pPatch->GetReferenceState(DataLocation_REdge);

		// Rayleigh friction strength
		const DataArray3D<double> & dataRayleighStrengthNode =
			pPatch->GetRayleighStrength(DataLocation_Node);

		const DataArray3D<double> & dataRayleighStrengthREdge =
			pPatch->GetRayleighStrength(DataLocation_REdge);

		// Loop over all nodes in patch
		for (int i = box.GetAInteriorBegin(); i < box.GetAInteriorEnd(); i++) {
		for (int j = box.GetBInteriorBegin(); j < box.GetBInteriorEnd(); j++) {

			// Rayleigh damping on nodes
			for (int k = 0; k < nRElements; k++) {

				double dNu = dataRayleighStrengthNode[i][j][k];

				// Backwards Euler
				if (dNu == 0.0) {
					continue;
				}

				double dNuNode = 1.0 / (1.0 + dDeltaT * dNu);

				// Loop over all effective components
				for (int c = 0; c < nComponents; c++) {
					if (pGrid->GetVarLocation(nEffectiveC[c]) ==
						DataLocation_Node) {
						for (int si = 0; si < nRayleighCycles; si++) {
							dNuNode = 1.0 / (1.0 + dRayleighFactor * dDeltaT * dNu);
							dataUpdateNode[nEffectiveC[c]][i][j][k] =
								dNuNode * dataUpdateNode[nEffectiveC[c]][i][j][k]
								+ (1.0 - dNuNode)
								* dataReferenceNode[nEffectiveC[c]][i][j][k];
						}
					}
				}
			}

			// Rayleigh damping on interfaces
			for (int k = 0; k <= nRElements; k++) {

				double dNu = dataRayleighStrengthREdge[i][j][k];

				// Backwards Euler
				if (dNu == 0.0) {
					continue;
				}

				double dNuREdge = 1.0 / (1.0 + dDeltaT * dNu);

				// Loop over all effective components
				for (int c = 0; c < nComponents; c++) {
					if (pGrid->GetVarLocation(nEffectiveC[c]) ==
						DataLocation_REdge) {
						for (int si = 0; si < nRayleighCycles; si++) {
							dNuREdge = 1.0 / (1.0 + dRayleighFactor * dDeltaT * dNu);
							dataUpdateREdge[nEffectiveC[c]][i][j][k] =
							dNuREdge * dataUpdateREdge[nEffectiveC[c]][i][j][k]
							+ (1.0 - dNuREdge)
							* dataReferenceREdge[nEffectiveC[c]][i][j][k];
						}
					}
				}
			}
		}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void SplitExplicitDynamics::StepAfterSubCycle(
	int iDataInitial,
	int iDataUpdate,
	int iDataWorking,
	const Time & time,
	double dDeltaT
) {
	// Start the function timer
	FunctionTimer timer("StepAfterSubCycle");

	return;

	// Check indices
	if (iDataInitial == iDataWorking) {
		_EXCEPTIONT("Invalid indices "
			"-- initial and working data must be distinct");
	}
	if (iDataUpdate == iDataWorking) {
		_EXCEPTIONT("Invalid indices "
			"-- working and update data must be distinct");
	}

	// Get the GLL grid
	GridGLL * pGrid = dynamic_cast<GridGLL*>(m_model.GetGrid());

	// Copy initial data to updated data
	pGrid->CopyData(iDataInitial, iDataUpdate, DataType_State);
	pGrid->CopyData(iDataInitial, iDataUpdate, DataType_Tracers);

	// No hyperdiffusion
	if ((m_dNuScalar == 0.0) && (m_dNuDiv == 0.0) && (m_dNuVort == 0.0)) {

	// Apply hyperdiffusion
	} else if (m_nHyperviscosityOrder == 0) {

	// Apply viscosity
	} else if (m_nHyperviscosityOrder == 2) {

		// Apply scalar and vector viscosity
		ApplyScalarHyperdiffusion(
			iDataInitial, iDataUpdate, dDeltaT, m_dNuScalar, false);
		ApplyVectorHyperdiffusion(
			iDataInitial, iDataUpdate, -dDeltaT, m_dNuDiv, m_dNuVort, false);

		// Apply positive definite filter to tracers
		FilterNegativeTracers(iDataUpdate);

		// Apply Direct Stiffness Summation
		pGrid->ApplyDSS(iDataUpdate, DataType_State);
		pGrid->ApplyDSS(iDataUpdate, DataType_Tracers);

	// Apply hyperviscosity
	} else if (m_nHyperviscosityOrder == 4) {

		// Apply scalar and vector hyperviscosity (first application)
		pGrid->ZeroData(iDataWorking, DataType_State);
		pGrid->ZeroData(iDataWorking, DataType_Tracers);

		ApplyScalarHyperdiffusion(
			iDataInitial, iDataWorking, 1.0, 1.0, false);
		ApplyVectorHyperdiffusion(
			iDataInitial, iDataWorking, 1.0, 1.0, 1.0, false);

		// Apply Direct Stiffness Summation
		pGrid->ApplyDSS(iDataWorking, DataType_State);
		pGrid->ApplyDSS(iDataWorking, DataType_Tracers);

		// Apply scalar and vector hyperviscosity (second application)
		ApplyScalarHyperdiffusion(
			iDataWorking, iDataUpdate, -dDeltaT, m_dNuScalar, true);
		ApplyVectorHyperdiffusion(
			iDataWorking, iDataUpdate, -dDeltaT, m_dNuDiv, m_dNuVort, true);

		// Apply positive definite filter to tracers
		FilterNegativeTracers(iDataUpdate);

		// Apply Direct Stiffness Summation
		pGrid->ApplyDSS(iDataUpdate, DataType_State);
		pGrid->ApplyDSS(iDataUpdate, DataType_Tracers);

	// Invalid viscosity order
	} else {
		_EXCEPTIONT("Invalid viscosity order");
	}

#ifdef APPLY_RAYLEIGH_WITH_HYPERVIS
		// Apply Rayleigh damping
		if (pGrid->HasRayleighFriction()) {
			ApplyRayleighFriction(iDataUpdate, dDeltaT);
		}
#endif
}

///////////////////////////////////////////////////////////////////////////////
