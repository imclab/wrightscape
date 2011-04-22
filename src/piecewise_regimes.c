/** 
 * @file piecewise_regimes.c
 * @author Carl Boettiger <cboettig@gmail.com>
 * @date 17 May 2010
 *
 * @section LICENSE 
 *
 * GPLv3
 *
 * @section DESCRIPTION
 *
 * Code for computing the likelihood of a multitype OU process on a phylogenetic tree
 *
 */



#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_errno.h>
#include "optimizers.h"
#include "mvn.h"

typedef struct {
	size_t n_nodes;
	size_t n_regimes;
	int * ancestor;
	int * regimes;
	double * branch_length;
	double * traits;
	double * alpha;
	double * theta;
	double * sigma;
	double * Xo;
	int * lca_matrix;
} tree;




/** get the last common ancestor of two nodes
 *  This version isn't particularly efficient, but the 
 *  calculation can in principle be done only once for 
 *  a given tree and stored as a matrix that is passed
 *  to the likelihood function.
 */
int get_lca(int i, int j, int n_nodes, const int * ancestor, const double * branch_length, double * sep)
	{
	int * ancestor_list = (int *) malloc(n_nodes * sizeof(int));
	int k = 0, s = 0;
	double * time_to_ancestor = (double *) calloc(n_nodes, sizeof(double));

	for(k=0; k<n_nodes; k++)
	{
		ancestor_list[k]=0;
	}
	k = 0;
	while(1){
		ancestor_list[k] = i;
		time_to_ancestor[k+1] = time_to_ancestor[k] + branch_length[i];
		if(i==0) break;
		i = ancestor[i];
		k++;
	}
	while(1){
		for(k=0; k<n_nodes; k++){
			if(j == ancestor_list[k]){ 
				s = j;
				j = 0;
				break; 
			}
		}
		if(j==0) break;
		j = ancestor[j];
	}
	*sep = time_to_ancestor[k];
	free(time_to_ancestor);
	free(ancestor_list);
	return s;	
}

/** Simple function to compute the age of a node 
 *  Called by calc_mean and calc_var */
double node_age(int i, const int * ancestor, const double * branch_length)
{
	double time=0;
	while(ancestor[i] >= 0 )
	{
		time += branch_length[i];
		i = ancestor[i];
	}
	return time; 
}

double log_normal_lik(int n, double * X_EX, double * V)
{
	gsl_matrix * V_inverse = gsl_matrix_alloc(n,n);
    gsl_permutation * p = gsl_permutation_alloc (n);
	gsl_matrix * ANS = gsl_matrix_alloc(1,n);
	gsl_matrix * ANS2 = gsl_matrix_alloc(1,1);
	double V_det, Xt_Vi_X;
	int signum;

	gsl_matrix_view V_view = gsl_matrix_view_array(V, n, n);
	gsl_matrix_view DIFF = gsl_matrix_view_array(X_EX, n, 1);
	gsl_linalg_LU_decomp (&V_view.matrix, p, &signum);
	gsl_linalg_LU_invert(&V_view.matrix, p, V_inverse);
	V_det = gsl_linalg_LU_det(&V_view.matrix,signum);

	/** @f$ -2 \log L = (X - E(X) )^T V^{-1} (X-E(X) ) + N\log(2\pi) + \log(\det V) @f$ */
	// Consider using appropriate blas optimized multiplication, not general matrix-matrix method!!
	gsl_blas_dgemm (CblasTrans, CblasNoTrans,
				   1.0, &DIFF.matrix, V_inverse,
				   0.0, ANS);
	gsl_blas_dgemm (CblasNoTrans, CblasNoTrans,
				   1.0, ANS, &DIFF.matrix,
				   0.0, ANS2);
	Xt_Vi_X = gsl_matrix_get(ANS2, 0, 0);

	gsl_matrix_free(ANS);
	gsl_matrix_free(ANS2);
	gsl_matrix_free(V_inverse);
	gsl_permutation_free(p);

	return -Xt_Vi_X/2. -  n*log(2*M_PI)/2. - log(V_det)/2.;
}


/** Calculate mean for likelihood computation 
 * 
 */
double calc_mean(
	int i,
	double Xo, ///< root state
	const double * alpha, ///< value of alpha in each regime, length n_regimes
	const double * theta, ///< value of theta in each regime
	const int * regimes, ///< specification of the regimes (paintings), length n_nodes
	const int * ancestor, ///< ancestor of the node, length n_nodes
	const double * branch_length, ///< branch length ancestral to the node, length n_nodes
	double * output_gamma
	)
{
	int ri;
	/* Find the time until the root, which must have ancestor -1 */
	double time = node_age(i, ancestor, branch_length);
	double prev_time;

	/* Compute E(X_i) */
	long double gamma=0, omega=0;
	while( ancestor[i] >= 0 )
	{
		prev_time = time - branch_length[i]; 
		ri = regimes[i];
		gamma += alpha[ri] * branch_length[i];
		omega += theta[ri] * ( exp(alpha[ri]*time) - exp(alpha[ri] * prev_time) );
		i = ancestor[i];
		time = prev_time;
	}
	*output_gamma = gamma;

	return exp(-gamma)*(Xo + omega);
}



/**
 * Calculate the variance matrix for the multivariate normal likelihood
 * Function would be faster if it could reuse the gamma values calculated for the means.  
 * Function would be much faster if it stored gamma
 */
double calc_var(
	int i, int j, ///< nodes being compared
	int lca, ///< last common ancestor, pass to avoid recalculating
	const double * alpha, ///< value of alpha in each regime, length n_regimes
	const double * sigma, ///< value of sigma in each regime
	const int * regimes, ///< specification of the regimes (paintings), length n_nodes
	const int * ancestor, ///< ancestor of the node, length n_nodes
	const double * branch_length, ///< branch length ancestral to the node, length n_nodes
	const double * gamma_vec
	)
{
/* FIXME typo in latex equation; breaks Doxygen documentation
 * @f[ E(X_t) = \exp \left( - \sum \alpha_i \Delta t_i \right) \left( X_i)
 * + \sum \theta_i \left( e^{\alpha_i t_i}-e^{\alpha_i t_{i-1} } \right) \right) */
	double gamma_i = gamma_vec[i], gamma_j = gamma_vec[j];

	double time = node_age(lca, ancestor, branch_length); 
	double prev_time;
	int ri;
	long double omega=0;

	i = lca;
	while( ancestor[i] >= 0) 
	{
		ri = regimes[i];
		prev_time = time - branch_length[i];
		omega += gsl_pow_2( sigma[ri] ) / (2 * alpha[ri] ) * (
			exp( 2 * alpha[ri] * time ) - exp( 2 * alpha[ri] * prev_time ) );
		time = prev_time;
		i = ancestor[i];
	}
	return exp(-gamma_i - gamma_j)*omega;
}


int * alloc_tips(int n_nodes, const int * ancestor){
	int n_tips = (n_nodes+1)/2;
	int * child = (int *) calloc(n_nodes,sizeof(int) );
	int * tips = (int *) calloc(n_tips,sizeof(int) );
	int i, j, k=0, empty;
	for(i=0; i < n_nodes; i++){ 
		empty = 1;
		for(j=0; j < n_nodes; j++){
			if(i == ancestor[j] ){
				if(empty){ 
					child[i] = j;
					empty = 0;
				}
			}
		}
		if (child[i] == 0){
			tips[k] = i;
			k++;
		}
	}
	free(child);
	return tips;
}


double calc_lik (
	const double *Xo, ///< root state
	const double * alpha, ///< value of alpha in each regime, length n_regimes
	const double * theta, ///< value of theta in each regime
	const double * sigma, ///< value of sigma in each regime
	const int * regimes, ///< specification of the regimes (paintings), length n_nodes
	const int * ancestor, ///< ancestor of the node, length n_nodes
	const double * branch_length, ///< branch length ancestral to the node, length n_nodes
	const double * traits, ///< traits
	int n_nodes, 
	int * lca_matrix  ///< much faster having this passed in rather than recalculatd
	)
{
	int i,j,ki, kj;
	int n_tips = (n_nodes+1)/2;
	double * X_EX = (double *) malloc(n_tips*sizeof(double));
	double * V = (double *) malloc(n_tips*n_tips*sizeof(double));
	double * gamma_vec = (double *) calloc(n_nodes,sizeof(double));

	double llik, mean, gamma_i;
	int lca;

	int * tips = alloc_tips(n_nodes, ancestor);
	double sep;
	for(i = 0; i < n_tips; i++){
		ki = tips[i];
		mean = calc_mean(ki, *Xo, alpha, theta, regimes, ancestor, branch_length, &gamma_i);
		X_EX[i] = traits[ki] - mean;
		gamma_vec[ki] = gamma_i;
	}
	for(i=0; i < n_tips; i++){
		ki = tips[i];
		for(j=0; j< n_tips; j++){
			kj = tips[j];
			lca = lca_matrix[ki*n_nodes+kj];
			V[n_tips*i+j] = calc_var(ki,kj,lca, alpha, sigma, regimes, ancestor, branch_length, gamma_vec);
		}
	}


	llik = log_normal_lik(n_tips, X_EX, V);
	free(gamma_vec);
	free(X_EX); 
	free(V);
	free(tips);
	return llik;
}


void simulate (const gsl_rng * rng, tree * mytree)
{
	int i,j,ki, kj;
	int n_tips = (mytree->n_nodes+1)/2;
	gsl_vector * EX = gsl_vector_alloc(n_tips);
	gsl_matrix * V = gsl_matrix_alloc(n_tips,n_tips);
	gsl_vector * simdata = gsl_vector_alloc(n_tips);
	double * gamma_vec = (double *) calloc(mytree->n_nodes,sizeof(double));
	double gamma_i;
	int lca;

	int * tips = alloc_tips(mytree->n_nodes, mytree->ancestor);
	for(i = 0; i < n_tips; i++){
		ki = tips[i];
		gsl_vector_set( EX,
						i,
						calc_mean(ki, *(mytree->Xo), mytree->alpha, 
								  mytree->theta, mytree->regimes, 
								  mytree->ancestor, mytree->branch_length, 
								  &gamma_i)
					   );
		gamma_vec[ki] = gamma_i;
	}
	for(i=0; i < n_tips; i++){
		ki = tips[i];
		for(j=0; j< n_tips; j++){
			kj = tips[j];
			lca = mytree->lca_matrix[ki*mytree->n_nodes+kj];
			gsl_matrix_set(	V, i, j, 
							calc_var(ki,kj,lca, mytree->alpha, 
									 mytree->sigma, mytree->regimes, 
									 mytree->ancestor, mytree->branch_length, 
									 gamma_vec)
						  );
		}
	}


	mvn(rng, EX, V, simdata);

	for(i=0; i< n_tips; i++){
		ki = tips[i];
		mytree->traits[ki] = gsl_vector_get(simdata,i);
	}

	gsl_vector_free(EX);
	gsl_matrix_free(V);
	free(tips);
	free(gamma_vec);
}




/* optim_func organizes  */
double optim_func (const gsl_vector *v, void *params)
{
	tree * mytree = (tree *) params;
	int i, n_regimes = mytree->n_regimes;
// Force root to match Theta of regime assigned to it, should also reduce vector size!
	mytree->Xo[0] = mytree->theta[mytree->regimes[0]];
 //	mytree->Xo[0] = gsl_vector_get(v, 0);
	for(i = 0; i < n_regimes; i++){
		mytree->alpha[i] = v->data[i];
		mytree->theta[i] = v->data[n_regimes+i];
		mytree->sigma[i] = v->data[2*n_regimes+i];

		if( (mytree->alpha[i] < 0) | (mytree->sigma[i] < 0) ){ return GSL_POSINF; }
		if(  mytree->theta[i] < 0 ){ return GSL_POSINF; }
	}
	
	return -calc_lik(mytree->Xo, mytree->alpha, mytree->theta, mytree->sigma, 
			 mytree->regimes, mytree->ancestor, mytree->branch_length, mytree->traits, mytree->n_nodes, mytree->lca_matrix); 
}


/** Fit the model by maximum likelihood, called by R */
void fit_model(double * Xo, 
	double * alpha, 
	double * theta, 
	double * sigma, 
	int * regimes, 
	int * ancestor, 
	double * branch_length, 
	double * traits, 
	int * n_nodes, 
	int * n_regimes,
	double * llik,
	int * use_siman)
{

//  Perhaps don't want to see gsl_errors once we're handling in R?
//	gsl_set_error_handler_off ();

	
    /** First, initialize the tree structure to facilitate passing all these elements */
	int i,j;
	tree * mytree = (tree  *) malloc(sizeof(tree));
	mytree->Xo = Xo;
	mytree->alpha = alpha;
	mytree->theta = theta;
	mytree->sigma = sigma;
	mytree->regimes = regimes;
	mytree->ancestor = ancestor;
	mytree->branch_length = branch_length;
	mytree->traits = traits;
	mytree->n_nodes = *n_nodes;
	mytree->n_regimes = *n_regimes;

    /** Second, save time by storing the least common ancestor matrix */
	/* Save time by calculating least common ancestor ahead of time.
	 * Though this does the calc for all nodes, only tip ones are used. 
	 * Current get_lca algorithm is only designed for tips anyway.  */
	double sep; // separation time, not actually used
	mytree->lca_matrix = (int *) malloc(gsl_pow_2(*n_nodes) * sizeof(int) );
	for(i=0; i < *n_nodes; i++){
		for(j=0; j < *n_nodes; j++){
			mytree->lca_matrix[*n_nodes*i+j] = get_lca(i,j, *n_nodes, ancestor, branch_length, &sep);
		}
	}

    /** Then initialize the vector of parameters to be searched */
	gsl_vector *x = gsl_vector_alloc(3 * *n_regimes);
//	gsl_vector_set(x, 0, *Xo);  /* Forces Xo to match theta (?) */
	for(i = 0; i < *n_regimes; i++){
		gsl_vector_set(x, i, alpha[i]);
		gsl_vector_set(x, *n_regimes+i, theta[i]);
		gsl_vector_set(x, 2 * *n_regimes+i, mytree->sigma[i]);
	}

    /** Select and call a likelihood optimization routine 
     *  given the tree (with data) and parameters */
	gsl_rng * rng = gsl_rng_alloc(gsl_rng_default);
	if(*use_siman)
		*llik = siman(x, mytree, rng); 
	else 
		*llik = multimin(x, mytree);


	gsl_rng_free(rng);
	gsl_vector_free(x);
	free(mytree->lca_matrix);
	free(mytree);
}



/* Direct calculation of likelihood, called by R (in progress FIXME) */
void likelihood_wrightscape(double * Xo, 
	double * alpha, 
	double * theta, 
	double * sigma, 
	int * regimes, 
	int * ancestor, 
	double * branch_length, 
	double * traits, 
	int * n_nodes, 
	int * n_regimes,
	double * llik)
{

//	gsl_set_error_handler_off ();

    /* Initialize the tree object */
	int i,j;
	tree * mytree = (tree  *) malloc(sizeof(tree));
	mytree->Xo = Xo;
	mytree->alpha = alpha;
	mytree->theta = theta;
	mytree->sigma = sigma;
	mytree->regimes = regimes;
	mytree->ancestor = ancestor;
	mytree->branch_length = branch_length;
	mytree->traits = traits;
	mytree->n_nodes = *n_nodes;
	mytree->n_regimes = *n_regimes;

    /* Consider passing the lca_matrix up to R for the likelihood function to reuse instead */

	/* Save time by calculating least common ancestor ahead of time.
	 * Though this does the calc for all nodes, only tip ones are used. 
	 * Current get_lca algorithm is only designed for tips anyway.  */
	double sep; // separation time, not actually used
	mytree->lca_matrix = (int *) malloc(gsl_pow_2(*n_nodes) * sizeof(int) );
	for(i=0; i < *n_nodes; i++){
		for(j=0; j < *n_nodes; j++){
			mytree->lca_matrix[*n_nodes*i+j] = get_lca(i,j, *n_nodes, ancestor, branch_length, &sep);
		}
	}

/* Allocate parameter vector */
gsl_vector *x = gsl_vector_alloc(3 * *n_regimes);

//	gsl_vector_set(x, 0, *Xo);  /* Force Xo to match theta */

/* Initialize the parameter vector */
	for(i = 0; i < *n_regimes; i++){
		gsl_vector_set(x, i, alpha[i]);
		gsl_vector_set(x, *n_regimes+i, theta[i]);
		gsl_vector_set(x, 2 * *n_regimes+i, mytree->sigma[i]);
	}

		*llik = multimin(x, mytree);

	gsl_vector_free(x);
	free(mytree->lca_matrix);
	free(mytree);
}




/** Simulate under the model.  Called by R */
void simulate_model(
	double * Xo, 
	double * alpha, 
	double * theta, 
	double * sigma, 
	int * regimes, 
	int * ancestor, 
	double * branch_length,
	double * traits,
	int * n_nodes, 
	int * n_regimes,
	double * llik, 
	double * seed
	)
{
	int i,j;
	tree * mytree = (tree  *) malloc(sizeof(tree));
	mytree->Xo = Xo;
	mytree->alpha = alpha;
	mytree->theta = theta;
	mytree->sigma = sigma;
	mytree->regimes = regimes;
	mytree->ancestor = ancestor;
	mytree->branch_length = branch_length;
	mytree->traits = traits;
	mytree->n_nodes = *n_nodes;
	mytree->n_regimes = *n_regimes;


	/* Save time by calculating least common ancestor ahead of time.
	 * Though this does the calc for all nodes, only tip ones are used. 
	 * Current get_lca algorithm is only designed for tips anyway.  */
	double sep; // separation time, not actually used
	mytree->lca_matrix = (int *) malloc(gsl_pow_2(*n_nodes) * sizeof(int) );
	for(i=0; i < *n_nodes; i++){
		for(j=0; j < *n_nodes; j++){
			mytree->lca_matrix[*n_nodes*i+j] = get_lca(i,j, *n_nodes, ancestor, branch_length, &sep);
		}
	}

	gsl_vector *x = gsl_vector_alloc(1 + 3 * *n_regimes);
	gsl_vector_set(x, 0, *Xo);
	for(i = 0; i < *n_regimes; i++){
		gsl_vector_set(x, 1+i, alpha[i]);
		gsl_vector_set(x, 1+*n_regimes+i, theta[i]);
		gsl_vector_set(x, 1+2 * *n_regimes+i, mytree->sigma[i]);
	}
	

	gsl_rng * rng = gsl_rng_alloc(gsl_rng_default);

	gsl_rng_set(rng, *seed);
	simulate (rng, mytree);

	*llik =  calc_lik(mytree->Xo, mytree->alpha, mytree->theta, mytree->sigma, 
			 mytree->regimes, mytree->ancestor, mytree->branch_length, mytree->traits, mytree->n_nodes, mytree->lca_matrix);

//	for(i=0; i< mytree->n_regimes; i++) printf("%g %g %g\n", mytree->alpha[i], mytree->theta[i], mytree->sigma[i] );
//	printf("sim llik: %g\n", *llik);
	
	gsl_rng_free(rng);
	gsl_vector_free(x);
	free(mytree->lca_matrix);
	free(mytree);
}





/** Test code by running in pure C */
int main(void)
{
/*	
	double branch_length[]	= {0, 1, 1, 3, 2, 1, 1}; 
	const int ancestor[]	={-1, 2, 0, 0, 2, 1, 1};
	const double traits[]	= {0, 0, 0, 5, 2, 1, 3};	
	const int regimes[]		= {0, 0, 0, 1, 0, 0, 0};
	int n_nodes = 7;

	double Xo = 1;
	double alpha[2] = {2, 2};
	double theta[2] = {1, 5}; 
	double sigma[2] = {1, 1};
/ */
	int n_nodes = 45;
	double branch_length[] = { 0.0, 12./38, 20./38,  2./38,  2./38,  4./38,  
							 8./38,  5./38,  5./38,  5./38,  5./38, 10./38,  
							 9./38,  4./38,  8./38,  2./38, 20./38,  2./38,  
							 4./38,  2./38, 1./38,  2./38, 26./38, 4./38, 
							 2./38, 2./38,  2./38,  2./38, 15./38, 10./38, 
							 10./38, 10./38, 10./38, 16./38, 12./38,  4./38,  
							 2./38,  2./38, 10./38,  8./38,  2./38,  1./38,  
							 1./38,  2./38,  2./38};

	int ancestor[] = {-1,  0,  1,  2,  3,  2,  0,  6,  7,  8,  9,
						8,  7, 12, 13,14,  6, 16, 17, 18, 19, 18, 1,
						3,  4,  4,  5,  5,  9, 10, 10, 11, 11, 12, 
						13, 14, 15, 15, 16, 17, 19, 20, 20, 21, 21};

	double traits[] = {   0.0000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
						  0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000,
						  0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 2.602690, 2.660260,
						  2.660260, 2.653242, 2.674149, 2.701361, 3.161247, 3.299534, 3.328627, 3.353407,
						  3.360375, 3.049273, 2.906901, 2.980619, 2.933857, 2.975530, 3.104587, 3.346389,
						  2.928524, 2.939162, 2.990720, 3.058707, 3.068053};
	//int regimes[45] = {0};
	int regimes[] =  {1, 1, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
						2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}; 

/*
	// ou1 parameters
	double Xo = 2.953806;
	double alpha[3] = { 0.192155, 0.192155, 0.192155};
	double theta[3] = {2.953806,  2.953806, 2.953806}; 
	double sigma[3] = {sqrt( .048365), sqrt( .048365), sqrt( .048365)};

	double Xo = 1;
	double alpha[3] = {1., 1., 1.};
	double theta[3] = {3., 3., 3.};
	double sigma[3] = {1.,  1., 1. };

*/

	double Xo = 3;
	double alpha[3] = {2.6, 2.6, 2.6};
	double theta[3] = {3., 3.,3};
	double sigma[3] = {.2,  .2, .2 };
	int n_regimes = 3;
	double llik = 0;

	int use_siman = 1;

	fit_model(&Xo, alpha, theta, sigma, regimes, ancestor, branch_length, traits, &n_nodes, &n_regimes, &llik, &use_siman);
	printf("Xo = %g\n", Xo);
	printf("alphas: %g %g %g\n", alpha[0], alpha[1], alpha[2]);
	printf("thetas: %g %g %g\n", theta[0], theta[1], theta[2]);
	printf("sigmas: %g %g %g\n", sigma[0], sigma[1], sigma[2]);

	printf("log likelihood: %g\n", llik);

	double seed = 1.0;
	printf("seed: %lo\n", (unsigned long int) seed);

	simulate_model(&Xo, alpha, theta, sigma, regimes, ancestor, branch_length, traits, &n_nodes, &n_regimes, &llik, &seed);

//	int i;
//	for(i=0; i < 45; i++) printf("%g\n", traits[i]);

	
	return 0;
}
