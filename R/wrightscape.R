#wrightscape.R

# update with new data
update.wrighttree <- function(ws, data){
	wrightscape(data=data, tree=ws$tree, regimes=ws$regimes, alpha=ws$alpha,
              sigma=ws$sigma, theta=ws$theta, Xo=ws$Xo)
}


simulate.wrighttree <- function(ws){
	output <- simulate_wrightscape(tree=ws$tree, regimes=ws$regimes,
                                   Xo=ws$Xo, alpha=ws$alpha,
                                   theta=ws$theta, sigma=ws$sigma)
  output$rep.1
}
loglik.wrighttree <- function(ws) ws$loglik

getParameters.wrighttree <- function(ws){
    c(alpha=ws$alpha, theta=ws$theta, sigma=ws$sigma, Xo=ws$Xo)
#, converge=ws$convergence) 
}

simulate.multiOU <- simulate.wrighttree
loglik.multiOU <- loglik.wrighttree
getParameters.multiOU <- getParameters.wrighttree


wrightscape <- function(data, tree, regimes, alpha=1, sigma=1, 
                        theta = NULL, Xo = NULL, use_siman=0){

	# data should be a numeric instead of data.frame.  
  # Should check node names or node order too!
	dataIn <- data
	if(is(data, "data.frame") | is(data, "list")) { 
		data <- data[[1]]
		if( !is(data, "numeric")) {
      stop("data should be data frame or numeric") 
    }
	}

	# regimes should be a factor instead of data.frame
	regimesIn <- regimes
	if(is(regimes, "data.frame")) { 
		regimes <- regimes[[1]]
		if( !is(regimes, "factor")) {
      stop("unable to interpret regimes") 
    }
	}


	if(is.null(Xo)){ Xo <- mean(data, na.rm=TRUE) }
	data[is.na(data)] = 0 


	ancestor <- as.numeric(tree@ancestors)
	ancestor[is.na(ancestor)] = 0 
	ancestor <- ancestor-1  # C-style indexing

	## ouch gives cumulative time, not branch-length!!
	anc <- as.integer(tree@ancestors[!is.na(tree@ancestors)])
	lengths <- c(0, tree@times[!is.na(tree@ancestors)] - tree@times[anc] )
	branch_length <- lengths/max(tree@times)

	
	n_nodes <- length(branch_length)
	n_regimes <- length(levels(regimes))

  # rep these if not specified
	if(length(alpha) == 1){ alpha <- rep(alpha, n_regimes) }
	if(is.null(theta)) { theta <- rep(Xo, n_regimes) }
	if(length(sigma) == 1) { sigma <- rep(sigma, n_regimes) }

  ## shouldn't be necessary
  if(is.list(alpha)) unlist(alpha)
  if(is.list(theta)) unlist(theta)
  if(is.list(sigma)) unlist(sigma)
  if(is.list(Xo)) unlist(Xo)


	levels(regimes) <- 1:n_regimes
	regimes <- as.integer(regimes)-1  # convert to C-style indexing


	o<- .C("fit_model",
		as.double(Xo),
		as.double(alpha),
		as.double(theta),
		as.double(sigma),
		as.integer(regimes),
		as.integer(ancestor),
		as.double(branch_length),
		as.double(data),
		as.integer(n_nodes),
		as.integer(n_regimes),
		double(1),
		as.integer(use_siman) #use the simulated annealing approach 
	  )

	output <- list(data=dataIn, tree=tree, regimes=regimesIn, loglik=o[[11]], Xo = o[[1]], alpha = o[[2]], theta =  o[[3]], sigma = o[[4]]  )  
	class(output) <- "wrighttree"
	output
}


simulate_wrightscape <- function(tree, regimes, Xo, alpha, theta, sigma){

	# regimes should be a factor instead of data.frame
	regimesIn <- regimes
	if(is(regimes, "data.frame")) { 
		regimes <- regimes[[1]]
		if( !is(regimes, "factor")) {stop("unable to interpret regimes") }
	}

	ancestor <- as.numeric(tree@ancestors)
	ancestor[is.na(ancestor)] = 0 
	ancestor <- ancestor-1  # C-style indexing

	## ouch gives cumulative time, not branch-length!!
	anc <- as.integer(tree@ancestors[!is.na(tree@ancestors)])
	lengths <- c(0, tree@times[!is.na(tree@ancestors)] - tree@times[anc] )
	branch_length <- lengths/max(tree@times)

	n_nodes <- length(branch_length)
	n_regimes <- length(levels(regimes))
	n_tips <- (n_nodes+1)/2

	levels(regimes) <- 1:n_regimes
	regimes <- as.integer(regimes)-1  # convert to C-style indexing

	seed <- runif(1)*2^16


	if(length(alpha) == 1){ alpha <- rep(alpha, n_regimes) }
	if(is.null(theta)) { theta <- rep(Xo, n_regimes) }
	if(length(sigma) == 1) { sigma <- rep(sigma, n_regimes) }



	o<- .C("simulate_model",
		as.double(Xo),
		as.double(alpha),
		as.double(theta),
		as.double(sigma),
		as.integer(regimes),
		as.integer(ancestor),
		as.double(branch_length),
		double(n_nodes),
		as.integer(n_nodes),
		as.integer(n_regimes),
		double(1), 
		as.double(seed)
	  )

	simdata <- data.frame(o[[8]], row.names = tree@nodes)
	

	output <- list(rep.1=simdata, tree=tree, regimes=regimesIn, loglik=o[[11]], alpha = o[[2]], theta =  o[[3]], sigma = o[[4]]  )  
	class(output) <- "wrighttree"
	output
}


## These should be part of an independent phylogenetic bootstrapping library (or at least file)
## Consider extending to some other functions, such as ape's ace fn, geiger's ancestral states, etc

LR_bootstrap <- function(true_model, test_model, nboot = 200){
# Bootstraps the likelihood ratio statistic using boot function
# Args:
#		true_model -- is used to generated the simulated data.  Must be a fitted hansentree or browntree
#		test_model -- is another fitted model whose likeihood will also be evaluated on the data
#		nboot -- is the number of bootstrap replicates to do.  Defaults to 200
#	Returns:
#		boot object that can be fed into boot.ci to 
#			generate confidence intervals, etc using the boot package

	get_loglik <- function(model){
		if(is(model, "ouchtree") ) loglik = model@loglik 
		else loglik = model$loglik
		loglik
	}
	get_data <- function(model){
		if(is(model, "ouchtree") ) data = model@data 
		else data = model$data
		data
	}

	orig_diff <- -2*( get_loglik(true_model) - get_loglik(test_model))
	orig_data <- get_data(true_model)

	statisticfn <- function(data, ...){
	# function required by boot fn that will be boostrapped
	# Args: 
	#		data is data generated by a simulation, 
	#		... is the test_model
	# Returns 
	#		the likelihood ratio statistic: 2*(log(null) - log(test) )
		test <- update(test_model, data=data)
		true <- update(true_model,data=data)
		-2*(get_loglik(true) - get_loglik(test))
	}

	rangendat <- function(d, p){
	# simulate using model specified as mle, the true model
		out <- simulate(p)
		out$rep.1
	}

	boot.out <- boot(	data=orig_data, 
						statistic=statisticfn, 
						R=nboot, 
						sim="parametric", 
						ran.gen=rangendat, 
						mle=true_model, 
						object=test_model)
}


fast_boot <- function(model, nboot=200, cpus=1){

	require(snowfall)
	sfInit(parallel=TRUE, cpus=cpus)
	sfExportAll()
	sfLibrary(wrightscape)

	fits <- sfLapply(1:nboot, function(i) update(model, data=simulate(model)$rep.1 ) )
	if(is(fits[[1]], "wrighttree") ){
		n_regimes <- length( fits[[1]]$sigma )
		n_pars <- 3*n_regimes+2
		regime_names <- levels(fits[[1]]$regimes[[1]])
		alpha_names <- sfSapply(1:n_regimes, function(i) paste("alpha.", regime_names[i]) )
		sigma_names <- sfSapply(1:n_regimes, function(i) paste("sigma.", regime_names[i]) )
		theta_names <- sfSapply(1:n_regimes, function(i) paste("theta.", regime_names[i]) )

		X <- sapply(1:nboot, function(i)  c(fits[[i]]$loglik, fits[[i]]$Xo, fits[[i]]$alpha, fits[[i]]$sigma, fits[[i]]$theta ) )
		rownames(X) <- c("loglik", "Xo", alpha_names, sigma_names, theta_names) 
	}
	out <- list(bootstrap_values = X, model=model, nboot=nboot)
	class(out) <- "wrightboot"
	out

}


plot.wrightboot <- function(input, CHECK_OUTLIERS=FALSE){
	object <- input$bootstrap_values
	par(mfrow=c(1,3) )
	n_regimes <- (dim(object)[1]-2)/3
	alphas <- 3:(2+n_regimes)
	sigmas <- (3+n_regimes):(2*n_regimes+2)
	thetas <- (3+2*n_regimes):(3*n_regimes+2)
	nboot <- dim(object)[2]
	outliers <- numeric(nboot)

	xlim <- c(0, 3*median(object[alphas,]) )
	ylim <- c(0, max(sapply(alphas, function(i) max(density(object[i,])$y))))

	plot(density(object[alphas[1], ]), xlim=xlim, ylim=ylim, xlab="Alpha values", type='n', main="", cex.lab=1.6, cex.axis = 1.6)
	k <- 1
	for(i in alphas){
		if(CHECK_OUTLIERS) outliers <- object[i,] > xlim[2]
		if( sum(outliers) > 0 ) print(paste(sum(outliers), " outliers in alpha ", k))
		lines(density(object[i,!outliers]), lwd = 3, lty=k)
		k <- k+1
	}

	xlim <- c(0, 3*median(object[sigmas,] ) )
	ylim <- c(0, max(sapply(sigmas, function(i) max(density(object[i,])$y))))
	plot(density(object[sigmas[1], ]), xlim=xlim, ylim=ylim, xlab="Sigma values", type='n', main="", cex.lab=1.6, cex.axis = 1.6)
	k <- 1
	for(i in sigmas){
		if(CHECK_OUTLIERS) outliers <- object[i,] > xlim[2]
		if( sum(outliers) > 0 ) print(paste(sum(outliers), " outliers in sigma ", k))
		lines(density(object[i,!outliers]), lwd = 3, lty=k)
		k <- k+1
	}

	xlim <- c(0, 3*median(object[thetas,] ) )
	ylim <- c(0, max(sapply(thetas, function(i) max(density(object[i,])$y))))
	plot(density(object[thetas[1], ]), xlim=xlim, ylim=ylim, xlab="Theta values", type='n', main="", cex.lab=1.6, cex.axis = 1.6)
	k <- 1
	for(i in thetas){
		if(CHECK_OUTLIERS) outliers <- object[i,] > xlim[2]
		if( sum(outliers) > 0 ) print(paste(sum(outliers), " outliers in theta ", k))
		lines(density(object[i,!outliers]), lwd = 3, lty=k)
		k <- k+1
	}
	legend("topright", levels(input$model$regimes), lty=1:length(levels(input$model$regimes)), lwd=2) 

}


bootstrap.wrighttree <- function(model, nboot = 200, fit=TRUE)
{
# Bootstraps the likelihood ratio statistic using boot function.  Should give bootstraps for all parameters!!!!
# Args:
#		model -- is used to generated the simulated data.  Must be a fitted hansentree or browntree
#		nboot -- is the number of bootstrap replicates to do.  Defaults to 200
#	Returns:
#		boot object -- can be fed into boot.ci to generate confidence intervals, etc 
#			using the boot package

	simdata <- simulate(model)
	refit_model <- update(model, data=simdata)
}

choose_model <- function(model_list, nboot=200, cpus=1){
	require(snowfall)
	sfInit(parallel=TRUE, cpus=cpus)
	sfExportAll()
	sfLibrary(wrightscape)

	LR <- sfLapply( 1:(length(model_list)-1),
					 function(i) LR_bootstrap( model_list[[i]], model_list[[i+1]], nboot )
		  		   )
	p_vals <- sfSapply( 1:(length(model_list)-1),
			function(i)  sum( LR[[i]]$t < LR[[i]]$t0 )/length(LR[[i]]$t)
		  )
	print(p_vals)
	LR	
}


pretty_plot <- function(LR, main=""){
	xlim = 1.1*c(min( LR$t, LR$t0), max( LR$t, LR$t0) )
	hist(LR$t, col="lightblue", border="white", xlab="Likelihood Ratio", cex.axis=1.6, cex.lab=1.6, main=main, xlim=xlim)
	abline(v=LR$t0, lwd=4, lty=2, col="darkblue")
	p_val <- 1-sum(LR$t < LR$t0)/length(LR$t)  
	text(LR$t0, 0.5*par()$yaxp[2], paste("p = ", round(p_val, digits=3)), cex=1.6)   # halfway up the vert line
}

# plot the wrightscape tree using the ouch plotting function
plot.wrighttree <- function(object)
{
	plot(object$tree, regimes=object$regimes)
}



