//implementation of the Saint Venant-Kirchhoff elastic material model
#ifndef STVENANTKIRSHHOFF_H
#define STVENANTKIRCHHOFF_H

//dealii headers
#include "../../../include/ellipticBVP.h"

//
//material model class for Saint Venant-Kirchhoff elastic model
//derives from ellipticBVP base abstract class
template <int dim>
class StVenantKirchhoff_Elastic : public ellipticBVP<dim>
{
public:
  StVenantKirchhoff_Elastic();
private:
  void markBoundaries();
  void applyDirichletBCs();
  void getElementalValues(FEValues<dim>& fe_values,
			  unsigned int dofs_per_cell,
			  unsigned int num_quad_points,
			  FullMatrix<double>& elementalJacobian,
			  Vector<double>&     elementalResidual);
};

//constructor
template <int dim>
StVenantKirchhoff_Elastic<dim>::StVenantKirchhoff_Elastic() : 
ellipticBVP<dim>()
{}

//implementation of the getElementalValues virtual method
template <int dim>
void StVenantKirchhoff_Elastic<dim>::getElementalValues(FEValues<dim>& fe_values,
							unsigned int dofs_per_cell,
							unsigned int num_quad_points,
							FullMatrix<double>& elementalJacobian,
							Vector<double>&     elementalResidual)
{
  //loop over quadrature points
  for (unsigned int q_point=0; q_point<num_quad_points;++q_point){
    for (unsigned int i=0; i<dofs_per_cell; ++i){
      const unsigned int component_i = this->FE.system_to_component_index(i).first;
      for (unsigned int j=0; j<dofs_per_cell; ++j){
	const unsigned int component_j = this->FE.system_to_component_index(j).first;

	// klocal= grad(N)*C_ijkl*grad(N)                                                                                                                          
	elementalJacobian(i,j) +=
	  (
	   (fe_values.shape_grad(i,q_point)[component_i] *
	    fe_values.shape_grad(j,q_point)[component_j] *
	    lambda_value)
	   +
	   (fe_values.shape_grad(i,q_point)[component_j] *
	    fe_values.shape_grad(j,q_point)[component_i] *
	    mu_value)
	   +
	   ((component_i == component_j) ?
	    (fe_values.shape_grad(i,q_point) *
	     fe_values.shape_grad(j,q_point) *
	     mu_value)  :
	    0)
	   )
	  *
	  fe_values.JxW(q_point);
      }
      elementalResidual(i) +=0.0;
    }
  }
}

#endif
