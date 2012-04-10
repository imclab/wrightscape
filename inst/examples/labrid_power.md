* Author: Carl Boettiger <cboettig@gmail.com>
* License: BSD 




```r
require(wrightscape)
require(ggplot2)
require(reshape)
data(labrids)
```






```r
traits <- c("bodymass", "close", "open", "kt", "gape.y",  "prot.y", "AM.y", "SH.y", "LP.y")
regimes <- two_shifts 
```




Fit all models, then actually perform the model choice analysis for the chosen model pairs



```r
fits <- lapply(traits, function(trait){
	multi <- function(modelspec){ 
	 multiTypeOU(data = dat[[trait]], tree = tree, regimes = regimes, 
			    model_spec = modelspec, control = list(maxit=8000))

	}
	bm <- multi(list(alpha = "fixed", sigma = "global", theta = "global")) 
	ou <- multi(list(alpha = "global", sigma = "global", theta = "global")) 
	bm2 <- multi(list(alpha = "fixed", sigma = "indep", theta = "global")) 
	a2  <- multi(list(alpha = "indep", sigma = "global", theta = "global")) 
	t2  <- multi(list(alpha = "global", sigma = "global", theta = "indep")) 

  mc <- montecarlotest(bm2,a2)
  bm2_a2 <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))
  mc <- montecarlotest(bm,ou)
  bm_ou <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))
  mc <- montecarlotest(bm,bm2)
  bm_bm2 <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))
  mc <- montecarlotest(ou,bm2)
  ou_bm2 <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))
  mc <- montecarlotest(t2,a2)
  t2_a2 <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))
  mc <- montecarlotest(bm2,t2)
  bm2_t2 <- list(null=mc$null_dist, test=mc$test_dist, lr=-2*(mc$null$loglik-mc$test$loglik))

  list(brownie_vs_alphas=bm2_a2, brownie_vs_thetas=bm2_t2, thetas_vs_alphas=t2_a2,
       bm_vs_brownie=bm_bm2,  bm_vs_ou=bm_ou, ou_vs_brownie=ou_bm2)
})
```



```
R Version:  R version 2.15.0 (2012-03-30) 

```



```
Library reshape loaded.
```



```
Library plyr loaded.
```



```
Library reshape2 loaded.
```



```
Library ggplot2 loaded.
```



```
Library wrightscape loaded.
```



```
Library mcmcTools loaded.
```



```
Library cairoDevice loaded.
```



```
Library Rflickr loaded.
```



```
Library digest loaded.
```



```
Library XML loaded.
```



```
Library RCurl loaded.
```



```
Library bitops loaded.
```



```
Library snowfall loaded.
```



```
Library snow loaded.
```



```
Library phytools loaded.
```



```
Library phangorn loaded.
```



```
Library Matrix loaded.
```



```
Library lattice loaded.
```



```
Library igraph loaded.
```



```
Library mnormt loaded.
```



```
Library geiger loaded.
```



```
Library ouch loaded.
```



```
Library subplex loaded.
```



```
Library msm loaded.
```



```
Library mvtnorm loaded.
```



```
Library MASS loaded.
```



```
Library ape loaded.
```



```
Library devtools loaded.
```



```
Library knitr loaded.
```




Clean up the data


```r
names(fits) <- traits
dat <- melt(fits)
names(dat) <- c("value", "type", "comparison", "trait")
```







```r
r <- cast(dat, comparison ~ trait, function(x) quantile(x, c(.10,.90)))
subdat <- subset(dat, abs(value) < max(abs(as.matrix(r))))
```




``` { r} 
ggplot(subdat) + 
  geom_boxplot(aes(type, value)) +
  facet_grid(trait ~ comparison, scales="free_y") 


Since it's tough to see everything on such a grid, plot individually:


```r
for(tr in traits){
  ggplot(subset(subdat, trait==tr)) +  geom_boxplot(aes(type, value)) +   facet_wrap(~ comparison, scales="free_y")
}
```



