#include "../../../include/crystalPlasticity.h"
#include "userFunctions.cc"

//////////////////////////////////////////////////////////////////////////
////calculatePlasticity.cc numerically integrates the constitive model.
////This calculatePlasticity.cc is based on the following rate-dependent crystal plasticity model:
////SR Kalidindi, Polycrystal plasticity: constitutive modeling and deformation processing,
//// PhD thesis, MIT, 1992. In addition to isotropic hardening tehre is also kinematic hardening
//// where the backstress evolves based on the OW hardening model. The guess stress to start the
//// Newton-Raphson iteration is the previously converged stress modified by the previous stress
//// increment. The tangent modulus construction is simple and similar to the one implemented in
//// MaterialModels/RateDependentModel .
////To use this file, one should copy this calculatePlasticity.cc into the following folder:
////    plasticity/src/materialModels/crystalPlasticity/
//// (It should replace the original calculatePlasticity.cc inside that folder)
////Next, they should copy the file userFunctions.cc into the following folder:
////    plasticity/src/materialModels/crystalPlasticity/
////
////Finally, the PRISMS-Plasticity should be recompiled.
////////////////////////////////////////////////////////////////////////////
//

template <int dim>
void crystalPlasticity<dim>::calculatePlasticity(unsigned int cellID,
  unsigned int quadPtID,
  unsigned int StiffnessCalFlag)
  {

    multiphaseInit(cellID,quadPtID);


    F_tau=F; // Deformation Gradient
    FullMatrix<double> FE_t(dim,dim),FP_t(dim,dim);  //Elastic and Plastic deformation gradient
    Vector<double> s_alpha_t(n_Tslip_systems),slipfraction_t(n_slip_systems),twinfraction_t(n_twin_systems); // Slip resistance
    Vector<double> W_kh_t(n_Tslip_systems),W_kh_t1(n_Tslip_systems),W_kh_t2(n_Tslip_systems),signed_slip_t(n_Tslip_systems) ;
    Vector<double> rot1(dim);// Crystal orientation (Rodrigues representation)
    FullMatrix<double> Tinter_diff_guess(dim,dim) ;


    // Tolerance

    double tol1=this->userInputs.modelStressTolerance;
    std::cout.precision(16);

    ////////////////////// The following parameters must be read in from the input file ///////////////////////////
    double delgam_ref = UserMatConstants(0)*this->userInputs.delT; // Reference slip increment
    double strexp=UserMatConstants(1); // Strain rate sensitivity exponent ; the higher the less sensitive
    double locres_tol=UserMatConstants(2); // Tolerance for residual of nonlinear constitutive model
    double locres_tol2=UserMatConstants(3); // Tolerance for outer slip resistance loop

    unsigned int nitr1=UserMatConstants(4); // Maximum number of iterations for constitutive model convergence
	unsigned int nitr2=UserMatConstants(5); // Maximum number of iterations for outer loop convergence


    ///////////////////////////////Backstress evolution parameters
    double h_back1=UserMatConstants(6);double h_back2=UserMatConstants(7);
    double r_back1=UserMatConstants(8);double r_back2=UserMatConstants(9);
    double m_back1=UserMatConstants(10);double m_back2=UserMatConstants(11);

	double b_back1,b_back2;

	// To deal with the scenario when there is no backstress evolution
    if(fabs(r_back1)>1.0e-7)
	    b_back1 = h_back1/r_back1 ;
    else
	    b_back1 = 1 ;

    if(fabs(r_back2)>1.0e-7)
	    b_back2 = h_back2/r_back2 ;
    else
	    b_back2 = 1 ;

    double back_lim_1 = b_back1 ;
    double back_lim_2 = b_back2 ;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////

    FE_t=Fe_conv[cellID][quadPtID];
    FP_t=Fp_conv[cellID][quadPtID];
    rot1=rot_conv[cellID][quadPtID];
    Tinter_diff_guess = TinterStress_diff[cellID][quadPtID] ;


    for(unsigned int i=0 ; i<n_Tslip_systems ; i++){
      W_kh_t1(i) = stateVar_conv[cellID][quadPtID][i];
      W_kh_t2(i) = stateVar_conv[cellID][quadPtID][i+n_Tslip_systems];
      signed_slip_t(i)=stateVar_conv[cellID][quadPtID][i+2*n_Tslip_systems];
      s_alpha_t(i)=s_alpha_conv[cellID][quadPtID][i];
    }
    for(unsigned int i=0 ; i<n_slip_systems ; i++)
    slipfraction_t(i) = slipfraction_conv[cellID][quadPtID][i] ;

    for(unsigned int i=0 ; i<n_twin_systems ; i++)
    twinfraction_t(i) = twinfraction_conv[cellID][quadPtID][i] ;


    // Rotation matrix of the crystal orientation
    FullMatrix<double> rotmat(dim,dim);
    rotmat=0.0;
    odfpoint(rotmat,rot1);

    FullMatrix<double> temp(dim,dim),temp1(dim,dim),temp2(dim,dim),temp3(dim,dim),temp4(dim,dim),temp5(dim,dim),temp6(dim,dim); // Temporary matrices
    FullMatrix<double> T_tau(dim,dim),P_tau(dim,dim),temp7(dim,dim), temp8(dim,dim);
    FullMatrix<double> FP_inv_t(dim,dim),FE_tau_trial(dim,dim),CE_tau_trial(dim,dim),Ee_tau_trial(dim,dim),FP_inv_tau(dim,dim),F_inv_tau(dim,dim);
    FullMatrix<double> SCHMID_TENSOR1(n_Tslip_systems*dim,dim),B(n_Tslip_systems*dim,dim),C(n_Tslip_systems*dim,dim),Normal_SCHMID_TENSOR1(n_Tslip_systems*dim,dim);
    Vector<double> m1(dim),n1(dim);

    // Elastic Modulus

    FullMatrix<double> Dmat(2*dim,2*dim),Dmat2(2*dim,2*dim),TM(dim*dim,dim*dim);
    Vector<double> vec1(2*dim),vec2(dim*dim);

    elasticmoduli(Dmat2, rotmat, elasticStiffnessMatrix);

    //Elastic Stiffness Matrix Dmat
    Dmat.reinit(6,6) ;
    Dmat = 0.0;

    for (unsigned int i = 0;i<6;i++) {
      for (unsigned int j = 0;j<6;j++) {
        Dmat[i][j] = Dmat2[i][j];
      }
    }


    for (unsigned int i = 0;i<6;i++) {
      for (unsigned int j = 3;j<6;j++) {
        Dmat[i][j] = 2 * Dmat[i][j];
      }
    }

    vec2(0)=0;vec2(1)=5;vec2(2)=4;vec2(3)=5;vec2(4)=1;vec2(5)=3;vec2(6)=4;vec2(7)=3;vec2(8)=2;



    for(unsigned int i=0;i<9;i++){
      for(unsigned int j=0;j<9;j++){
        TM[i][j]=Dmat2(vec2(i),vec2(j));
      }
    }



    Vector<double> s_alpha_tau(n_Tslip_systems),slipfraction_tau(n_slip_systems),twinfraction_tau(n_twin_systems) ;
    Vector<double> W_kh_tau(n_Tslip_systems),W_kh_tau1(n_Tslip_systems),W_kh_tau2(n_Tslip_systems), W_kh_tau_it(n_Tslip_systems),W_kh_t1_it(n_Tslip_systems),W_kh_t2_it(n_Tslip_systems);
    Vector<double> h_beta(n_Tslip_systems),h0(n_Tslip_systems),a_pow(n_Tslip_systems),s_s(n_Tslip_systems);
    FullMatrix<double> h_alpha_beta_t(n_Tslip_systems,n_Tslip_systems);
    Vector<double> resolved_shear_tau(n_Tslip_systems),normal_stress_tau(n_Tslip_systems),signed_slip_tau(n_Tslip_systems);

    FullMatrix<double> PK1_Stiff(dim*dim,dim*dim);
    FullMatrix<double> T_star_tau(dim,dim),T_star_tau_trial(dim,dim),mtemp(dim,dim);
    Vector<double> vtemp(2*dim),tempv1(2*dim),bck_vec(2*dim);
    double det_FE_tau, det_F_tau, det_FP_tau;

    FP_inv_t = 0.0; FP_inv_t.invert(FP_t);
    FE_tau_trial=0.0;
    F_tau.mmult(FE_tau_trial,FP_inv_t) ;

    // CE_tau_trial is the same as A matrix - Kalidindi's thesis
    CE_tau_trial=0.0;
    FE_tau_trial.Tmmult(CE_tau_trial,FE_tau_trial);

    temp.reinit(dim,dim);
    Ee_tau_trial=CE_tau_trial;
    temp=IdentityMatrix(dim);
    Ee_tau_trial = 0 ;
	Ee_tau_trial.add(0.5,CE_tau_trial,-0.5,temp) ;

    // Calculate the trial stress T_star_tau_trial

    tempv1=0.0;
    Dmat.vmult(tempv1, vecform(Ee_tau_trial));
    matform(T_star_tau_trial,tempv1);

    // Loop over slip systems to construct relevant matrices - Includes both slip and twin(considered as pseudo-slip) systems
    for (unsigned int i = 0;i<n_Tslip_systems;i++) {

      for (unsigned int j = 0;j<dim;j++) {
        m1(j) = m_alpha[i][j];
        n1(j) = n_alpha[i][j];
      }

      temp = 0.0;
      temp2 = 0.0;
      for (unsigned int j = 0;j<dim;j++) {
        for (unsigned int k = 0;k<dim;k++) {
          temp[j][k] = m1(j)*n1(k);         // Construct Schmid tensor matrix relative to the crystal reference frame
		  temp7[j][k] = n1(j)*n1(k);
        }
      }
      // Transform the Schmid tensor matrix to sample coordinates
      rotmat.mmult(temp2, temp);
      temp2.mTmult(temp, rotmat);

      for (unsigned int j = 0;j<dim;j++) {
        for (unsigned int k = 0;k<dim;k++) {
          SCHMID_TENSOR1[dim*i + j][k] = temp[j][k]; // Store rotated Schmid tensor matrix
		  Normal_SCHMID_TENSOR1[dim*i + j][k] = temp7[j][k];
        }
      }

      // Construct B matrix - Kalidindi's thesis
      CE_tau_trial.mmult(temp2, temp);
      temp2.symmetrize();
      vtemp=0.0;
      mtemp=0.0;
      temp2.equ(2.0,temp2);
      // Construct C matrix - Kalidindi's thesis
      Dmat.vmult(vtemp, vecform(temp2));
      matform(mtemp,vtemp);
      mtemp.equ(0.5,mtemp);

      for (unsigned int j = 0;j<dim;j++) {
        for (unsigned int k = 0;k<dim;k++) {
          B[dim*i + j][k] = temp2[j][k];
          C[dim*i + j][k] = mtemp[j][k];
        }
      }


    }

    W_kh_tau1 = W_kh_t1 ;
    W_kh_tau2 = W_kh_t2 ;
    W_kh_tau = 0.0 ;
    W_kh_tau.add(1.0,W_kh_t1,1.0,W_kh_t2) ;
    W_kh_tau_it = W_kh_tau ;

    slipfraction_tau = slipfraction_t ;
    signed_slip_tau  = signed_slip_t;
    twinfraction_tau = twinfraction_t ;


    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////Start Nonlinear iteration for Slip increments////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned int itr1=0, itr2 = 0;
    double sctmp1,sctmp2, sctmp3, sctmp4;
    Vector<double> G_iter(2*dim),locres_vec(2*dim+2*n_Tslip_systems),stateVar_it(2*dim+2*n_Tslip_systems),stateVar_temp(2*dim+2*n_Tslip_systems),stateVar_diff(2*dim+2*n_Tslip_systems);
    FullMatrix<double> btemp1(2*dim,2*dim),J_iter(2*dim+2*n_Tslip_systems,2*dim+2*n_Tslip_systems),J_iter_inv(2*dim+2*n_Tslip_systems,2*dim+2*n_Tslip_systems);                                   FullMatrix<double> T_star_iter(dim,dim),T_star_iterp(dim,dim);
    FullMatrix<double> CE_t(dim,dim);
    Vector<double> s_alpha_it(n_Tslip_systems),s_alpha_iterp(n_Tslip_systems),delgam_tau(n_Tslip_systems),delgam_tau_iter(n_Tslip_systems);

    Vector<double> vtmp1(2*dim),vtmp2(2*dim),vtmp3(2*dim),vtmp4(n_Tslip_systems);
    Vector<double> nv1(2*dim),nv2(2*dim),T_star_iter_vec(2*dim);
    double locres = 1.0, locres2 = 1.0  ;
	double sgnm, sgnm2 , sgnm3 , sgnm4 ;

    FE_t.Tmmult(CE_t,FE_t);
    T_star_iter.equ(1.0,T_star_tau_trial);
	temp =0 ;
	temp1= IdentityMatrix(dim) ;
	temp.add(0.5,CE_t,-0.5,temp1) ;
	Dmat.vmult(vtmp1,vecform(temp)) ;
	T_star_iter = 0 ;
	matform(T_star_iter,vtmp1) ;
    T_star_iter.add(1.0,Tinter_diff_guess) ;


    s_alpha_it=s_alpha_t;

    nv1 = vecform(T_star_iter) ;

	for(unsigned int j=0;j<2*dim;j++){
		stateVar_it(j) = nv1(j) ;
	}

	for(unsigned int j=0;j<n_Tslip_systems;j++){
		stateVar_it(j+2*dim) = W_kh_tau1(j) ;
	}

	for(unsigned int j=0;j<n_Tslip_systems;j++){
		stateVar_it(j+2*dim+n_Tslip_systems) = W_kh_tau2(j) ;
	}

	while(locres2>locres_tol2 && itr2 < nitr2){

    itr2 = itr2 + 1  ;



    itr1 = 0 ;

    locres = 1.0 ;

      // Loop until the residual is greater than tolerance or the  number of iterations crosses a certain specified maximum
      while(locres>locres_tol && itr1<nitr1){

        itr1 = itr1+1;

        // Residual for the non-linear algebraic equation
        G_iter=0.0;

        // Jacobian for the Newton-Raphson iteration
        J_iter=IdentityMatrix(2*dim+2*n_Tslip_systems);
		locres_vec = 0.0 ;

		for (unsigned int j = 0;j < 2*dim;j++) {
			nv1(j) = stateVar_it(j) ;
		}
		matform(T_star_iter,nv1) ;


		for (unsigned int j = 0;j < n_Tslip_systems;j++) {
			W_kh_t1_it(j) = stateVar_it(j+2*dim) ;
		}

		for (unsigned int j = 0;j < n_Tslip_systems;j++) {
			W_kh_t2_it(j) = stateVar_it(j+2*dim+n_Tslip_systems) ;
		}

		W_kh_tau_it = 0.0 ;
		W_kh_tau_it.add(1.0,W_kh_t1_it,1.0,W_kh_t2_it) ;


        // Loop over slip systems
        for (unsigned int i = 0;i<n_Tslip_systems;i++){

          for (unsigned int j = 0;j < dim;j++) {
            for (unsigned int k = 0;k < dim;k++) {
              temp[j][k]=SCHMID_TENSOR1[dim*i + j][k];
              temp4[j][k]=C[dim*i + j][k];
            }
          }

          T_star_iter.mTmult(temp2,temp);
          sctmp1=temp2.trace();

          if(i<n_slip_systems){ // For slip systems due to symmetry of slip
            if((sctmp1-W_kh_tau_it(i))<0)
            sgnm=-1;
            else
            sgnm=1 ;
          }
          else               // For twin systems due to asymmetry of slip
          {
            if((sctmp1-W_kh_tau_it(i))<=0)
            sgnm=0;
            else
            sgnm=1 ;
          }

          delgam_tau(i) = delgam_ref*pow(fabs((sctmp1 - W_kh_tau_it(i))/s_alpha_it(i)),1.0/strexp)*sgnm ;

          temp2 = 0.0 ;
          temp2.add(1.0,temp) ;
          temp2.Tadd(1.0,temp) ;

          nv1 = vecform(temp4);
          nv2 = vecform(temp2);
          nv2(1) = nv2(1)/2.0 ;
          nv2(2) = nv2(2)/2.0 ;
          nv2(3) = nv2(3)/2.0 ;


          // Modification to the residual inside loop
          G_iter.add(delgam_tau(i),nv1);

          for (unsigned int j = 0;j < 2*dim;j++) {
            for (unsigned int k = 0;k < 2*dim;k++) {
              btemp1[j][k]=nv1(j)*nv2(k);
            }
          }

          if(i<n_slip_systems){
            sgnm = 1 ;
          }
          else{
            if((sctmp1-W_kh_tau_it(i))<=0)
            sgnm=0;
            else
            sgnm=1 ;
          }
          sctmp2=delgam_ref/(strexp*s_alpha_it(i))*pow(fabs((sctmp1-W_kh_tau_it(i))/s_alpha_it(i)),(1.0/strexp - 1.0))*sgnm;


          btemp1.equ(sctmp2,btemp1);

          // Modification to the Jacobian of the Newton-Raphson iteration

		  // Components 1:6
		  for (unsigned int j = 0;j < 2*dim;j++) {
            for (unsigned int k = 0;k < 2*dim;k++) {
          J_iter[j][k] = J_iter[j][k] + btemp1(j,k);
			}
		  }

		  vtmp1.equ(-1.0*sctmp2,nv1) ;
		  for (unsigned int j = 0;j < 2*dim;j++) {
	          J_iter[j][i+2*dim] = J_iter[j][i+2*dim] + vtmp1(j);
		  J_iter[j][i+2*dim+n_Tslip_systems] = J_iter[j][i+2*dim+n_Tslip_systems] + vtmp1(j);
		  }

		  // Components rest
		  if(i<n_slip_systems){ // For slip systems due to symmetry of slip
            if((sctmp1-W_kh_tau_it(i))<0)
            sgnm=-1;
            else
            sgnm=1 ;
          }
          else               // For twin systems due to asymmetry of slip
          {
            if((sctmp1-W_kh_tau_it(i))<=0)
            sgnm=0;
            else
            sgnm=1 ;
          }

		  // Backstress component 1

            if(W_kh_t1_it(i)<0)
            sgnm2=-1;
            else
            sgnm2=1 ;


		  sctmp3 = r_back1*pow(fabs(W_kh_t1_it(i)/b_back1),m_back1)*W_kh_t1_it(i)*sgnm*sctmp2 ;
		  sctmp3 = sctmp3 - h_back1*sctmp2 ;


		  for (unsigned int j = 0;j < 2*dim;j++) {
			  J_iter(i+2*dim,j) = sctmp3*nv2(j) ;
		  }

		  J_iter(i+2*dim,i+2*dim) = J_iter(i+2*dim,i+2*dim) - sctmp3 ;
		  J_iter(i+2*dim,i+2*dim+n_Tslip_systems) = J_iter(i+2*dim,i+2*dim+n_Tslip_systems) - sctmp3 ;


		  J_iter(i+2*dim,i+2*dim) = J_iter(i+2*dim,i+2*dim) + abs(delgam_tau(i))*r_back1*pow(fabs(W_kh_t1_it(i)/b_back1),m_back1) ;
		  J_iter(i+2*dim,i+2*dim) = J_iter(i+2*dim,i+2*dim) + abs(delgam_tau(i))*W_kh_t1_it(i)*r_back1*m_back1/b_back1*pow(fabs(W_kh_t1_it(i)/b_back1),m_back1-1.0)*sgnm2 ;
		  locres_vec(i+2*dim) = W_kh_t1_it(i) - W_kh_t1(i) - h_back1*delgam_tau(i) + r_back1*pow(fabs(W_kh_t1_it(i)/b_back1),m_back1)*W_kh_t1_it(i)*fabs(delgam_tau(i)) ;



		  // Backstress component 2



		  sctmp4 = r_back2*pow(fabs(W_kh_t2_it(i)/b_back2),m_back2)*W_kh_t2_it(i)*sgnm*sctmp2 ;
		  sctmp4 = sctmp4 - h_back2*sctmp2 ;



            if(W_kh_t2_it(i)<0)
            sgnm2=-1;
            else
            sgnm2=1 ;


		  for (unsigned int j = 0;j < 2*dim;j++) {
			  J_iter(i+2*dim+n_Tslip_systems,j) = sctmp4*nv2(j) ;
		  }


		  J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) = J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) - sctmp4 ;
		  J_iter(i+2*dim+n_Tslip_systems,i+2*dim) = J_iter(i+2*dim+n_Tslip_systems,i+2*dim) - sctmp4 ;


		  J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) = J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) + abs(delgam_tau(i))*r_back2*pow(fabs(W_kh_t2_it(i)/b_back2),m_back2) ;
		  J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) = J_iter(i+2*dim+n_Tslip_systems,i+2*dim+n_Tslip_systems) + abs(delgam_tau(i))*W_kh_t2_it(i)*r_back2*m_back2/b_back2*pow(fabs(W_kh_t2_it(i)/b_back2),m_back2-1.0)*sgnm2 ;
		  locres_vec(i+2*dim+n_Tslip_systems) = W_kh_t2_it(i) - W_kh_t2(i) - h_back2*delgam_tau(i) + r_back2*pow(fabs(W_kh_t2_it(i)/b_back2),m_back2)*W_kh_t2_it(i)*fabs(delgam_tau(i)) ;


        }
        // Final modification to the residual outside the loop
        G_iter.add(1.0,vecform(T_star_iter),-1.0,vecform(T_star_tau_trial));

		 for (unsigned int j = 0;j < 2*dim;j++) {
			  locres_vec(j) = G_iter(j) ;
		  }


        // Invert Jacobian
        J_iter_inv.invert(J_iter);
        J_iter_inv.vmult(stateVar_diff,locres_vec);
		stateVar_diff.equ(-1.0,stateVar_diff) ;
		stateVar_temp = 0.0 ;
		stateVar_temp.add(1.0,stateVar_it,1.0,stateVar_diff);

		for (unsigned int j = 0;j < n_Tslip_systems;j++) {

            if(stateVar_temp(2*dim+j)<0)
            sgnm2=-1;
            else
            sgnm2=1 ;

	if(fabs(stateVar_temp(2*dim+j)) >= back_lim_1)
			  stateVar_temp(2*dim+j) = back_lim_1*sgnm2 ;
 	  }


		 for (unsigned int j = 0;j < n_Tslip_systems;j++) {

            if(stateVar_temp(2*dim+j+n_Tslip_systems)<0)
            sgnm2=-1;
            else
            sgnm2=1 ;

	    if(fabs(stateVar_temp(2*dim+j+n_Tslip_systems)) >= back_lim_2)
		  stateVar_temp(2*dim+j+n_Tslip_systems) = back_lim_2*sgnm2 ;


		  }

		stateVar_diff = 0.0 ;
		stateVar_diff.add(1.0,stateVar_temp,-1.0,stateVar_it);

		locres = stateVar_diff.l2_norm() ;
		stateVar_it.equ(1.0,stateVar_temp) ;

        } // inner while

	for(unsigned int j=0;j<2*dim;j++){
      T_star_iter_vec(j) = stateVar_it(j) ;
    }

	matform(T_star_iter,T_star_iter_vec) ;




    for(unsigned int j=0;j<n_Tslip_systems;j++){
      W_kh_tau1(j) = stateVar_it(j+2*dim)  ;
      W_kh_tau2(j) = stateVar_it(j+2*dim+n_Tslip_systems) ;
      W_kh_tau(j) = W_kh_tau1(j) + W_kh_tau2(j) ;

    }


	// Single slip hardening rate
      for(unsigned int i=0;i<n_slip_systems;i++){
        h_beta(i)=initialHardeningModulus[i]*pow((1-s_alpha_it(i)/saturationStress[i]),powerLawExponent[i]);
      }


      for(unsigned int i=0;i<n_twin_systems;i++){
        h_beta(n_slip_systems+i)=initialHardeningModulusTwin[i]*pow((1-s_alpha_it(n_slip_systems+i)/saturationStressTwin[i]),powerLawExponentTwin[i]);
      }


      for(unsigned int i=0;i<n_Tslip_systems;i++){
        for(unsigned int j=0;j<n_Tslip_systems;j++){
          h_alpha_beta_t[i][j] = q[i][j]*h_beta(j);
        }
      }

      s_alpha_iterp = s_alpha_t;

      temp=0.0;
      temp1=0.0;
      temp2=0.0;
      temp3=0.0;
      temp4=0.0;
      temp5=0.0;
      temp6=0.0;
      sctmp1=0.0;
      vtmp1=0.0;
      vtmp2=0.0;

      for (unsigned int i = 0;i<n_Tslip_systems;i++){
        for (unsigned int j = 0;j < dim;j++) {
          for (unsigned int k = 0;k < dim;k++) {
            temp[j][k]=SCHMID_TENSOR1[dim*i + j][k];
          }
        }

        T_star_iter.mTmult(temp1,temp);
        sctmp1=temp1.trace();
        resolved_shear_tau(i)=sctmp1;
	    sctmp1 = sctmp1 - W_kh_tau(i) ;

        if(i<n_slip_systems){ // For slip systems due to symmetry of slip
          if(sctmp1<0)
          sgnm=-1;
          else
          sgnm=1 ;
        }
        else               // For twin systems due to asymmetry of slip
        {
          if(sctmp1<=0)
          sgnm=0;
          else
          sgnm=1 ;
        }


        delgam_tau(i)=delgam_ref*pow(fabs((resolved_shear_tau(i)-W_kh_tau(i))/s_alpha_it(i)),(1.0/strexp))*sgnm;

        for (unsigned int j = 0;j<n_Tslip_systems;j++){
          s_alpha_iterp(j)=s_alpha_iterp(j)+h_alpha_beta_t[j][i]*fabs(delgam_tau(i));
        }

      }
        // Check if the slip system resistances exceed their corresponding saturation stress. If yes, set them equal to the saturation stress

        for(unsigned int i=0;i<n_slip_systems;i++){
          if(s_alpha_iterp[i] >= saturationStress[i])
          s_alpha_iterp[i] =  saturationStress[i] ;
        }


        for(unsigned int i=0;i<n_twin_systems;i++){
          if(s_alpha_iterp[n_slip_systems+i] >= saturationStressTwin[i])
          s_alpha_iterp[n_slip_systems+i] =  saturationStressTwin[i] ;
        }

      vtmp4=0.0;
      vtmp4.add(1.0,s_alpha_iterp,-1.0,s_alpha_it);
      locres2=vtmp4.l2_norm();
      s_alpha_it=s_alpha_iterp;



    } // outer while
    ////////////////////////////////////End Nonlinear iteration for Slip increments////////////////////////////////////

    s_alpha_tau=s_alpha_it;
    for(unsigned int j=0 ; j<2*dim ; j++)
	nv1(j) = stateVar_it(j) ;

    matform(T_star_iter,nv1) ;


    for(unsigned int j=0 ; j<n_Tslip_systems ; j++){
	W_kh_tau1(j) = stateVar_it(2*dim+j) ;
    W_kh_tau2(j) = stateVar_it(2*dim+j+n_Tslip_systems) ;
	W_kh_tau(j) = W_kh_tau1(j) + W_kh_tau2(j) ;
	}

    FP_tau=IdentityMatrix(dim);
    temp=0.0;

    for (unsigned int i = 0;i<n_Tslip_systems;i++){
      for (unsigned int j = 0;j < dim;j++) {
        for (unsigned int k = 0;k < dim;k++) {
          temp[j][k]=SCHMID_TENSOR1[dim*i + j][k];
        }
      }

	T_star_iter.mTmult(temp2,temp);
        resolved_shear_tau(i)=temp2.trace();

      if(i<n_slip_systems){ // For slip systems due to symmetry of slip
        if((resolved_shear_tau(i) - W_kh_tau(i))<0)
        sgnm=-1;
        else
        sgnm=1 ;
      }
      else               // For twin systems due to asymmetry of slip
      {
        if((resolved_shear_tau(i) - W_kh_tau(i))<=0)
        sgnm=0;
        else
        sgnm=1 ;
      }

      delgam_tau(i)=delgam_ref*pow(fabs((resolved_shear_tau(i) - W_kh_tau(i))/s_alpha_tau(i)),(1.0/strexp))*sgnm;
      FP_tau.add(-1.0*delgam_tau(i),temp);
    }

    for(unsigned int i=0 ; i<n_slip_systems ; i++)
    slipfraction_tau(i) = slipfraction_t(i) + fabs(delgam_tau(i)) ;

    for(unsigned int i=0 ; i<n_Tslip_systems ; i++)
    signed_slip_tau(i) = signed_slip_t(i) + delgam_tau(i) ;

    for(unsigned int i=0 ; i<n_twin_systems ; i++)
    twinfraction_tau(i) = twinfraction_t(i) + fabs(delgam_tau(i+n_slip_systems)) ;

    temp=0.0;
    temp.invert(FP_tau);
    temp.mmult(FP_tau,FP_t);

    det_FP_tau=FP_tau.determinant();
    FP_tau.equ(pow(det_FP_tau,-1.0/3),FP_tau);

    FP_inv_tau = 0.0; FP_inv_tau.invert(FP_tau);
    FE_tau = 0.0;
    F_tau.mmult(FE_tau, FP_inv_tau);
    temp.reinit(dim, dim);
    det_FE_tau = FE_tau.determinant();
    T_star_tau.equ(1.0,T_star_iter);
    FE_tau.mmult(temp, T_star_tau);
    temp.equ(1.0 / det_FE_tau, temp);
    temp.mTmult(T_tau, FE_tau);

    det_F_tau = F_tau.determinant();
    temp.invert(F_tau);
    F_inv_tau.equ(1.0,temp);
    T_tau.mTmult(P_tau, temp);
    P_tau.equ(det_F_tau, P_tau);

	for (unsigned int i = 0;i<n_Tslip_systems;i++){
      temp7=0.0;
      temp8=0.0;
      for (unsigned int j = 0;j < dim;j++) {
        for (unsigned int k = 0;k < dim;k++) {
          temp7[j][k]=Normal_SCHMID_TENSOR1[dim*i + j][k];
        }
      }

      T_star_tau.mTmult(temp8,temp7);
      normal_stress_tau(i)=temp8.trace();

    }


    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////// Computing Algorithmic Tangent Modulus ////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    FullMatrix<double>  cnt1(dim*dim,dim*dim),cnt2(dim*dim,dim*dim),cnt3(dim*dim,dim*dim),cnt4(dim*dim,dim*dim); // Variables to track individual contributions
    FullMatrix<double> dFedF(dim*dim,dim*dim); // Meaningful variables
    FullMatrix<double>  ntemp1(dim*dim,dim*dim),ntemp2(dim*dim,dim*dim),ntemp3(dim*dim,dim*dim),ntemp4(dim*dim,dim*dim),ntemp5(dim*dim,dim*dim),ntemp6(dim*dim,dim*dim); // Temporary variables

    double modifier1_num, modifier1_den, modifier2_num, modifier2_den, modifier;

    double mulfac;

    // Contribution 1 - Most straightforward because no need to invoke constitutive model
      FE_tau.mmult(temp,T_star_tau);
      temp.mTmult(temp1,FE_tau);
      temp1.mTmult(temp2,F_inv_tau);
      left(ntemp1,temp2);
      left(ntemp2,F_inv_tau);
      trpose(ntemp3,ntemp2);
      ntemp1.mmult(cnt4,ntemp3);
      cnt4.equ(-1.0,cnt4);

      // Compute dFedF
      ntemp4=0.0;
      for (unsigned int i = 0;i<n_Tslip_systems;i++){
        for (unsigned int j = 0;j < dim;j++) {
          for (unsigned int k = 0;k < dim;k++) {
            temp[j][k]=SCHMID_TENSOR1[dim*i + j][k];
          }
        }

        traceval(ntemp1,temp);

        temp1=0.0;
        temp1.add(0.5,temp);
        temp1.Tadd(0.5,temp);

        Dmat.vmult(vtmp1,vecform(temp1));
        matform(temp1,vtmp1);
        temp1.mTmult(temp2,FE_tau);
        left(ntemp2,temp2);
        ntemp1.mmult(ntemp3,ntemp2);


        if(i<n_slip_systems){
          sgnm = 1 ;
        }
        else{
          if((resolved_shear_tau(i) - W_kh_tau(i))<=0)
          sgnm=0;
          else
          sgnm=1 ;
        }

        mulfac = delgam_ref*1.0/strexp*1.0/s_alpha_tau(i)*pow(fabs((resolved_shear_tau(i) - W_kh_tau(i))/s_alpha_tau(i)),1.0/strexp - 1.0)*sgnm;


        ///////////////////Added lines by Aadi//////////////
        // ******** Corresponding to slip and twin slip increments *********
        if(i<n_slip_systems){
          if((resolved_shear_tau(i) - W_kh_tau(i))<=0)
          sgnm2 = -1 ;
          else
          sgnm2 = 1 ;
        }
        else{
          if((resolved_shear_tau(i) - W_kh_tau(i))<=0)
          sgnm2=0;
          else
          sgnm2=1 ;
        }

        // ********** Corresponding to each of the backstress components  **********
        // Component 1
        if(W_kh_tau1(i)<0)
        sgnm3 = -1 ;
        else
        sgnm3 = 1 ;
        // Component 2
        if(W_kh_tau2(i)<0)
        sgnm4 = -1 ;
        else
        sgnm4 = 1 ;

        // Assemble modifiers
        modifier1_num = h_back1 - r_back1*pow(fabs(W_kh_tau1(i))/b_back1,m_back1)*W_kh_tau1(i)*sgnm2 ;
        modifier2_num = h_back2 - r_back2*pow(fabs(W_kh_tau2(i))/b_back2,m_back2)*W_kh_tau2(i)*sgnm2 ;

        modifier1_den = 1.0 + r_back1*pow(fabs(W_kh_tau1(i))/b_back1,m_back1)*fabs(delgam_tau(i)) ;
        sctmp1 =  r_back1*W_kh_tau1(i)*fabs(delgam_tau(i))*m_back1*sgnm3/b_back1 ;
        sctmp1 = sctmp1*pow(fabs(W_kh_tau1(i))/b_back1,m_back1-1.0) ;
        modifier1_den = modifier1_den + sctmp1 ;

        modifier2_den = 1.0 + r_back2*pow(fabs(W_kh_tau2(i))/b_back2,m_back2)*fabs(delgam_tau(i)) ;
        sctmp1 =  r_back2*W_kh_tau2(i)*fabs(delgam_tau(i))*m_back2*sgnm4/b_back2 ;
        sctmp1 = sctmp1*pow(fabs(W_kh_tau2(i))/b_back2,m_back2-1.0) ;
        modifier2_den = modifier2_den + sctmp1 ;

        modifier = modifier1_num/modifier1_den + modifier2_num/modifier2_den ;
        mulfac=mulfac/(1.0+mulfac*modifier);

        //////////////////Added Lines by Aadi///////////////
        ntemp4.add(mulfac,ntemp3);
      }

      left(ntemp1,FE_tau_trial);
      ntemp1.mmult(ntemp2,ntemp4);
      temp1=IdentityMatrix(dim);
      left(ntemp3,temp1);
      ntemp2.add(1.0,ntemp3);
      right(ntemp3,FP_inv_tau);
      ntemp1.invert(ntemp2);
      ntemp1.mmult(dFedF,ntemp3);


      // Compute remaining contributions which depend solely on dFedF

      // Contribution 1
      T_star_tau.mTmult(temp1,FE_tau);
      temp1.mmult(temp2,F_inv_tau);
      right(ntemp1,temp2);
      ntemp1.mmult(cnt1,dFedF);

      // Contribution 2
      temp=0.0;
      temp.Tadd(1.0,FE_tau);
      left(ntemp1,temp);
      TM.mmult(ntemp2,ntemp1);
      ntemp1.equ(0.5,ntemp2);

      left(ntemp2,temp);
      trpose(ntemp3,ntemp2);
      TM.mmult(ntemp2,ntemp3);
      ntemp2.equ(0.5,ntemp2);

      ntemp3=0.0;
      ntemp3.add(1.0,ntemp1,1.0,ntemp2);
      ntemp3.mmult(ntemp4,dFedF);

      left(ntemp1,FE_tau);

      temp1=0.0;
      temp1.Tadd(1.0,FE_tau);
      temp1.mTmult(temp2,F_inv_tau);
      right(ntemp2,temp2);

      ntemp1.mmult(ntemp3,ntemp4);
      ntemp3.mmult(cnt2,ntemp2);

      // Contribution 3
      FE_tau.mmult(temp1,T_star_tau);
      left(ntemp1,temp1);

      left(ntemp2,F_inv_tau);
      ntemp2.mmult(ntemp3,dFedF);
      trpose(ntemp4,ntemp3);
      ntemp1.mmult(cnt3,ntemp4);


      // Assemble contributions to PK1_Stiff

      PK1_Stiff=0.0;
      PK1_Stiff.add(1.0,cnt1);
      PK1_Stiff.add(1.0,cnt2);
      PK1_Stiff.add(1.0,cnt3);
      PK1_Stiff.add(1.0,cnt4);

      ////////////////// End Computation ////////////////////////////////////////


      dP_dF = 0.0;
      FullMatrix<double> L(dim, dim);
      L = IdentityMatrix(dim);
      for (unsigned int m = 0;m<dim;m++) {
        for (unsigned int n = 0;n<dim;n++) {
          for (unsigned int o = 0;o<dim;o++) {
            for (unsigned int p = 0;p<dim;p++) {
              for (unsigned int i = 0;i<dim;i++) {
                for (unsigned int j = 0;j<dim;j++) {
                  for (unsigned int k = 0;k<dim;k++) {
                    for (unsigned int l = 0;l<dim;l++) {
                      dP_dF[m][n][o][p] = dP_dF[m][n][o][p] + PK1_Stiff(dim*i + j, dim*k + l)*L(i, m)*L(j, n)*L(k, o)*L(l, p);
                    }
                  }
                }
              }
            }
          }
        }
      }



    P.reinit(dim, dim);
    P = P_tau;
    T = T_tau;

    T_inter.reinit(dim,dim) ;
	T_inter = T_star_tau ;

    sres_tau.reinit(n_Tslip_systems);
    sres_tau = s_alpha_tau;

    // Update the history variables
    Fe_iter[cellID][quadPtID]=FE_tau;
    Fp_iter[cellID][quadPtID]=FP_tau;
    s_alpha_iter[cellID][quadPtID]=sres_tau;
    W_kh_iter[cellID][quadPtID]=W_kh_tau;

    for(unsigned int i=0 ; i<n_Tslip_systems ; i++){
      stateVar_iter[cellID][quadPtID][i]=W_kh_tau1(i);
      stateVar_iter[cellID][quadPtID][i+n_Tslip_systems]=W_kh_tau2(i);
      stateVar_iter[cellID][quadPtID][i+2*n_Tslip_systems]=signed_slip_tau(i);
      stateVar_iter[cellID][quadPtID][i+3*n_Tslip_systems]=normal_stress_tau(i);
    }

    for(unsigned int i=0 ; i<n_twin_systems ; i++)
    twinfraction_iter[cellID][quadPtID][i]=twinfraction_tau(i);

    for(unsigned int i=0 ; i<n_slip_systems ; i++)
    slipfraction_iter[cellID][quadPtID][i]=slipfraction_tau(i);

    /////// EXTRA STUFF FOR REORIENTATION POST TWINNING ////////////////////

    std::vector<double> local_twin;
    std::vector<double>::iterator result;
    double twin_pos, twin_max;
    Vector<double> quat1(4), rod(3), quat2(4), quatprod(4);

    if (enableTwinning){
      if (!this->userInputs.enableMultiphase){
        if (F_r > 0) {
          F_T = twinThresholdFraction + (twinSaturationFactor*F_e / F_r);
        }
        else {
          F_T = twinThresholdFraction;
        }
      }
      else{
        F_T = twinThresholdFraction;
      }

      //////Eq. (13) in International Journal of Plasticity 65 (2015) 61–84
      if (F_T > 1.0) {
        F_T = 1.0;
      }
      local_twin.resize(n_twin_systems,0.0);
      local_twin=twinfraction_iter[cellID][quadPtID];
      result = std::max_element(local_twin.begin(), local_twin.end());
      twin_pos= std::distance(local_twin.begin(), result);
      twin_max=local_twin[twin_pos];
      if (twin_conv[cellID][quadPtID] != 1.0) {
        if(F_r>0){
          if(twin_max > F_T){

            rod(0) = rot_conv[cellID][quadPtID][0];rod(1) = rot_conv[cellID][quadPtID][1];rod(2) = rot_conv[cellID][quadPtID][2];
            odfpoint(rotmat, rod);
            rod2quat(quat2, rod);
            quat1(0) = 0;
            quat1(1) = n_alpha[n_slip_systems + twin_pos][0];
            quat1(2) = n_alpha[n_slip_systems + twin_pos][1];
            quat1(3) = n_alpha[n_slip_systems + twin_pos][2];

            quatproduct(quatprod, quat2, quat1);


            quat2rod(quatprod, rod);

            odfpoint(rotmat, rod);

            rot_iter[cellID][quadPtID][0] = rod(0);rot_iter[cellID][quadPtID][1] = rod(1);rot_iter[cellID][quadPtID][2] = rod(2);
            rotnew_iter[cellID][quadPtID][0] = rod(0);rotnew_iter[cellID][quadPtID][1] = rod(1);rotnew_iter[cellID][quadPtID][2] = rod(2);
            twin_iter[cellID][quadPtID] = 1.0;
            for (unsigned int i = 0;i < n_twin_systems;i++) {
              s_alpha_iter[cellID][quadPtID][n_slip_systems + i] =100000;
            }
          }
        }
      }
    }


  }
  #include "../../../include/crystalPlasticity_template_instantiations.h"
