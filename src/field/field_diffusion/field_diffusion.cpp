//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//  \brief Class to implement diffusion processes in the induction equations

#include <algorithm>  // min()
#include <cfloat>     // FLT_MA

// Athena++ headers
#include "field_diffusion.hpp"
#include "../../athena.hpp"
#include "../../athena_arrays.hpp"
#include "../../mesh/mesh.hpp"
#include "../../coordinates/coordinates.hpp"
#include "../field.hpp"
#include "../../hydro/hydro.hpp"
#include "../../parameter_input.hpp"

// FieldDiffusion constructor

FieldDiffusion::FieldDiffusion(MeshBlock *pmb, ParameterInput *pin)
{
  pmy_block = pmb;
  field_diffusion_defined = false;

  int nthreads = pmb->pmy_mesh->GetNumMeshThreads();
  int ncells1 = pmb->block_size.nx1 + 2*(NGHOST);
  int ncells2 = 1, ncells3 = 1;
  if (pmb->block_size.nx2 > 1) ncells2 = pmb->block_size.nx2 + 2*(NGHOST);
  if (pmb->block_size.nx3 > 1) ncells3 = pmb->block_size.nx3 + 2*(NGHOST);

  // Check if field diffusion
  coeff_o = pin->GetOrAddReal("problem","coef_o",0.0);
  coeff_h = pin->GetOrAddReal("problem","coef_h",0.0);
  coeff_a = pin->GetOrAddReal("problem","coef_a",0.0);

  if ((coeff_o != 0.0) || (coeff_h != 0.0) || (coeff_a != 0.0)) {
    field_diffusion_defined = true;
    // Allocate memory for scratch vectors
    etaB.NewAthenaArray(3,ncells3,ncells2,ncells1);
    e_oa.x1e.NewAthenaArray(ncells3+1,ncells2+1,ncells1);
    e_oa.x2e.NewAthenaArray(ncells3+1,ncells2,ncells1+1);
    e_oa.x3e.NewAthenaArray(ncells3,ncells2+1,ncells1+1);
    e_h.x1e.NewAthenaArray(ncells3+1,ncells2+1,ncells1);
    e_h.x2e.NewAthenaArray(ncells3+1,ncells2,ncells1+1);
    e_h.x3e.NewAthenaArray(ncells3,ncells2+1,ncells1+1);
    pflux.x1f.NewAthenaArray(ncells3,ncells2,ncells1+1);
    pflux.x2f.NewAthenaArray(ncells3,ncells2+1,ncells1);
    pflux.x3f.NewAthenaArray(ncells3+1,ncells2,ncells1);

    jfx.NewAthenaArray(3,ncells3+1,ncells2+1,ncells1+1);
    jfy.NewAthenaArray(3,ncells3+1,ncells2+1,ncells1+1);
    jfz.NewAthenaArray(3,ncells3+1,ncells2+1,ncells1+1);
    jcc.NewAthenaArray(3,ncells3+1,ncells2+1,ncells1+1);

    eta_tot_.NewAthenaArray(nthreads,ncells1);
    bmag_.NewAthenaArray(ncells3,ncells2,ncells1);

    jedge_.x1e.NewAthenaArray(ncells3+1,ncells2+1,ncells1);
    jedge_.x2e.NewAthenaArray(ncells3+1,ncells2,ncells1+1);
    jedge_.x3e.NewAthenaArray(ncells3,ncells2+1,ncells1+1);

    face_area_.NewAthenaArray(nthreads,ncells1);
    face_area_p1_.NewAthenaArray(nthreads,ncells1);
    edge_length_.NewAthenaArray(nthreads,ncells1);
    edge_length_m1_.NewAthenaArray(nthreads,ncells1);
    cell_volume_.NewAthenaArray(nthreads,ncells1);
    dx1_.NewAthenaArray(nthreads,ncells1);
    dx2_.NewAthenaArray(nthreads,ncells1);
    dx3_.NewAthenaArray(nthreads,ncells1);

    if(pmb->pmy_mesh->FieldDiffusivity_==NULL)
      CalcMagDiffCoeff_ = ConstDiffusivity;
    else
      CalcMagDiffCoeff_ = pmb->pmy_mesh->FieldDiffusivity_;
  }
}

// destructor

FieldDiffusion::~FieldDiffusion()
{
  if ((coeff_o != 0.0) || (coeff_h != 0.0) || (coeff_a != 0.0)) {
    etaB.DeleteAthenaArray();
    e_oa.x1e.DeleteAthenaArray();
    e_oa.x2e.DeleteAthenaArray();
    e_oa.x3e.DeleteAthenaArray();
    e_h.x1e.DeleteAthenaArray();
    e_h.x2e.DeleteAthenaArray();
    e_h.x3e.DeleteAthenaArray();
    pflux.x1f.DeleteAthenaArray();
    pflux.x2f.DeleteAthenaArray();
    pflux.x3f.DeleteAthenaArray();

    jfx.DeleteAthenaArray();
    jfy.DeleteAthenaArray();
    jfz.DeleteAthenaArray();
    jcc.DeleteAthenaArray();

    eta_tot_.DeleteAthenaArray();
    bmag_.DeleteAthenaArray();

    jedge_.x1e.DeleteAthenaArray();
    jedge_.x2e.DeleteAthenaArray();
    jedge_.x3e.DeleteAthenaArray();

    face_area_.DeleteAthenaArray();
    face_area_p1_.DeleteAthenaArray();
    edge_length_.DeleteAthenaArray();
    edge_length_m1_.DeleteAthenaArray();
    cell_volume_.DeleteAthenaArray();
    dx1_.DeleteAthenaArray();
    dx2_.DeleteAthenaArray();
    dx3_.DeleteAthenaArray();
  }
}

//----------------------------------------------------------------------------------------
//! \fn void FieldDiffusion::CalcFieldDiffusionEMF
//  \brief Calculate diffusion EMF(Ohmic & Ambipolar for now)

void FieldDiffusion::CalcFieldDiffusionEMF(FaceField &bi,
     const AthenaArray<Real> &bc, EdgeField &e)
{
  Field *pf = pmy_block->pfield;
  Hydro *ph = pmy_block->phydro;
  Mesh  *pm = pmy_block->pmy_mesh;

  if((coeff_o==0.0) && (coeff_a==0.0)) return;

  SetFieldDiffusivity(ph->w,pf->bcc);

  //CalcCurrent(pfield->b);
  CalcCurrent(bi);
  ClearEMF(e_oa);
  if (coeff_o != 0.0)
    //OhmicEMF(pfield->b, pfield->bcc, e_oa);
    OhmicEMF(bi, bc, e_oa);
  if (coeff_a != 0.0)
    //AmbipolarEMF(pfield->b, pfield->bcc, e_oa);
    AmbipolarEMF(bi, bc, e_oa);

  // calculate the Poynting flux pflux and add to energy flux in Hydro class
  if (NON_BAROTROPIC_EOS) {
    //PoyntingFlux(e_oa, pfield->bcc);
    PoyntingFlux(e_oa, bc);
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn void FieldDiffusion::AddEMF(EdgeField &e_src, EdgeField &e_des)
//  \brief Add source EMF to destination EMF

void FieldDiffusion::AddEMF(const EdgeField &e_src, EdgeField &e_des)
{
  int size1 = e_src.x1e.GetSize();
  int size2 = e_src.x2e.GetSize();
  int size3 = e_src.x3e.GetSize();

#pragma omp simd
  for (int i=0; i<size1; ++i)
    e_des.x1e(i) += e_src.x1e(i);

#pragma omp simd
  for (int i=0; i<size2; ++i)
    e_des.x2e(i) += e_src.x2e(i);

#pragma omp simd
  for (int i=0; i<size3; ++i)
    e_des.x3e(i) += e_src.x3e(i);

  return;
}

//----------------------------------------------------------------------------------------
//! \fn void FieldDiffusion::ClearFieldDiffusionEMF(EdgeField &e)
//  \brief Clear EMF

void FieldDiffusion::ClearEMF(EdgeField &e)
{
  int size1 = e.x1e.GetSize();
  int size2 = e.x2e.GetSize();
  int size3 = e.x3e.GetSize();

#pragma omp simd
  for (int i=0; i<size1; ++i)
    e.x1e(i) = 0.0;

#pragma omp simd
  for (int i=0; i<size2; ++i)
    e.x2e(i) = 0.0;

#pragma omp simd
  for (int i=0; i<size3; ++i)
    e.x3e(i) = 0.0;

  return;
}


//--------------------------------------------------------------------------------------
// Set magnetic diffusion coefficients

void FieldDiffusion::SetFieldDiffusivity(const AthenaArray<Real> &w, const AthenaArray<Real> &bc)
{
  MeshBlock *pmb = pmy_block;
  int il = pmb->is-NGHOST; int jl = pmb->js; int kl = pmb->ks;
  int iu = pmb->ie+NGHOST; int ju = pmb->je; int ku = pmb->ke;
  if (pmb->block_size.nx2 > 1) {
    jl -= NGHOST; ju += NGHOST;
  }
  if (pmb->block_size.nx3 > 1) {
    kl -= NGHOST; ku += NGHOST;
  }

  int nthreads = pmb->pmy_mesh->GetNumMeshThreads();
#pragma omp parallel default(shared) num_threads(nthreads)
{

  for (int k=kl; k<=ku; ++k){
#pragma omp for schedule(static)
    for (int j=jl; j<=ju; ++j){
#pragma omp simd
      for (int i=il; i<=iu; ++i){
        Real Bsq = SQR(bc(IB1,k,j,i)) + SQR(bc(IB2,k,j,i)) + SQR(bc(IB3,k,j,i));
        bmag_(k,j,i) = sqrt(Bsq);
      }
    }
  }
  // set diffusivities
  CalcMagDiffCoeff_(this, w, bmag_, il, iu, jl, ju, kl, ku);

} // end of omp parallel region

  return;
}

//--------------------------------------------------------------------------------------
// Add Poynting flux to the hydro energy flux

void FieldDiffusion::AddPoyntingFlux(FaceField &p_src)
{
  MeshBlock *pmb=pmy_block;
  AthenaArray<Real> &x1flux=pmb->phydro->flux[X1DIR];
  AthenaArray<Real> &x2flux=pmb->phydro->flux[X2DIR];
  AthenaArray<Real> &x3flux=pmb->phydro->flux[X3DIR];
  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;

  AthenaArray<Real> f1,f2,f3;
  f1.InitWithShallowCopy(p_src.x1f);
  f2.InitWithShallowCopy(p_src.x2f);
  f3.InitWithShallowCopy(p_src.x3f);

  // 1D update:
  if (pmb->block_size.nx2 == 1) {
#pragma omp simd
    for (int i=is; i<=ie+1; ++i) {
      x1flux(IEN,ks,js,i) += f1(ks,js,i);
    }
    return;
  }

  int nthreads = pmb->pmy_mesh->GetNumMeshThreads();
#pragma omp parallel default(shared) private(tid) num_threads(nthreads)
  {
    // 2D update:
    if (pmb->block_size.nx3 == 1) {
#pragma omp for schedule(static)
      for (int j=js; j<=je+1; ++j) {
#pragma omp simd
        for (int i=is; i<=ie+1; ++i) {
          x1flux(IEN,ks,j,i) += f1(ks,j,i);
          x2flux(IEN,ks,j,i) += f2(ks,j,i);
        }
      }
      return;
    }

    // 3D update:
    for (int k=ks; k<=ke+1; ++k) {
#pragma omp for schedule(static)
      for (int j=js; j<=je+1; ++j) {
#pragma omp simd
        for (int i=is; i<=ie+1; ++i) {
          x1flux(IEN,k,j,i) += f1(k,j,i);
          x2flux(IEN,k,j,i) += f2(k,j,i);
          x3flux(IEN,k,j,i) += f3(k,j,i);
        }
      }
    }
  } // end of OMP parallel region
  return;
}

//--------------------------------------------------------------------------------------
// Get the non-ideal MHD timestep

void FieldDiffusion::NewFieldDiffusionDt(Real &dt_oa, Real &dt_h)
{
  int tid=0;
  MeshBlock *pmb = pmy_block;
  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;

  Real fac_oa,fac_h;
  if(pmb->block_size.nx3>1) fac_oa = 1.0/6.0;
  else if (pmb->block_size.nx2>1) fac_oa = 0.25;
  else fac_oa = 0.5;
  if(pmb->block_size.nx2>1) fac_h = 1.0;
  else fac_h=0.5;
  //if(pmb->block_size.nx3>1) fac_oa = 1.0/3.0;
  //else fac_oa=0.5;
  //if(pmb->block_size.nx2>1) fac_h = 1.0;
  //else fac_h=0.5;

  int nthreads = pmb->pmy_mesh->GetNumMeshThreads();
  Real *ptd_mindt_oa;
  Real *ptd_mindt_h;
  ptd_mindt_oa = new Real [nthreads];
  ptd_mindt_h  = new Real [nthreads];

  for (int n=0; n<nthreads; ++n) {
    ptd_mindt_oa[n] = (FLT_MAX);
    ptd_mindt_h[n]  = (FLT_MAX);
  }

#pragma omp parallel default(shared) private(tid) num_threads(nthreads)
{
#ifdef OPENMP_PARALLEL
  tid=omp_get_thread_num();
#endif
  AthenaArray<Real> eta_t;
  eta_t.InitWithShallowSlice(eta_tot_,2,tid,1);
  AthenaArray<Real> len, dx2, dx3;
  len.InitWithShallowSlice(dx1_,2,tid,1);
  dx2.InitWithShallowSlice(dx2_,2,tid,1);
  dx3.InitWithShallowSlice(dx3_,2,tid,1);

  for (int k=ks; k<=ke; ++k){

#pragma omp for schedule(static)
    for (int j=js; j<=je; ++j){
#pragma omp simd
      for (int i=is; i<=ie; ++i){
        eta_t(i) = 0.0;
      }
      if (coeff_o > 0.0){
#pragma omp simd
        for (int i=is; i<=ie; ++i){
          eta_t(i) += etaB(I_O,k,j,i);
        }
      }
      if (coeff_a > 0.0){
#pragma omp simd
        for (int i=is; i<=ie; ++i){
          eta_t(i) += etaB(I_A,k,j,i);
        }
      }
      pmb->pcoord->CenterWidth1(k,j,is,ie,len);
      pmb->pcoord->CenterWidth2(k,j,is,ie,dx2);
      pmb->pcoord->CenterWidth3(k,j,is,ie,dx3);
#pragma omp simd
      for (int i=is; i<=ie; ++i){
        len(i) = (pmb->block_size.nx2 > 1) ? std::min(len(i),dx2(i)):len(i);
        len(i) = (pmb->block_size.nx3 > 1) ? std::min(len(i),dx3(i)):len(i);
      }
      if ((coeff_o > 0.0) || (coeff_a > 0.0)) {
#pragma omp simd
        for (int i=is; i<=ie; ++i)
          ptd_mindt_oa[tid] = std::min(ptd_mindt_oa[tid], fac_oa*SQR(len(i))/(eta_t(i)+TINY_NUMBER));
      }
      if (coeff_h > 0.0) {
#pragma omp simd
        for (int i=is; i<=ie; ++i)
          ptd_mindt_h[tid]= std::min(ptd_mindt_h[tid], fac_h*SQR(len(i))/(fabs(etaB(I_H,k,j,i))+TINY_NUMBER));
      }
    }
  }
} // end of omp parallel region

  dt_oa = ptd_mindt_oa[0];
  dt_h = ptd_mindt_h[0];
  for (int n=1; n<nthreads; ++n) {
    dt_oa = std::min(dt_oa,ptd_mindt_oa[n]);
    dt_h  = std::min(dt_h, ptd_mindt_h[n]);
  }

  delete[] ptd_mindt_oa;
  delete[] ptd_mindt_h;

  return;
}
