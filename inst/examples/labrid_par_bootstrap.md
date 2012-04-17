* File: labrid.R
* Author: Carl Boettiger <cboettig@gmail.com>
* License: BSD 





```r
require(wrightscape)
require(snowfall)
require(ggplot2)
require(reshape2)
require(Hmisc)
```








```r
data(labrids)
traits <- c("bodymass", "close", "open", "kt", "gape.y",  "prot.y", "AM.y", "SH.y", "LP.y")
regimes <- two_shifts
```




Setup parallel environment



```r
sfInit(par=T, 4)   
```



```
R Version:  R version 2.14.1 (2011-12-22) 

```



```r
sfLibrary(wrightscape)
```



```
Library wrightscape loaded.
```



```r
sfExportAll()
```




Bootstrap the fits by replication (bootstraps parameter values without bootstrapping the likelihood ratios)


```r
fits <- sfLapply(traits, function(trait){
  multi <- function(modelspec, reps = 100){
    m <- multiTypeOU(data = dat[trait], tree = tree, regimes = regimes, 
  		     model_spec = modelspec, 
		     control = list(maxit=8000)
		    ) 
    replicate(reps, bootstrap(m))
  }
  bm <- multi(list(alpha = "fixed", sigma = "global", theta = "global")) 
  ou <- multi(list(alpha = "global", sigma = "global", theta = "global")) 
  bm2 <- multi(list(alpha = "fixed", sigma = "indep", theta = "global")) 
  a2  <- multi(list(alpha = "indep", sigma = "global", theta = "global")) 
  t2  <- multi(list(alpha = "global", sigma = "global", theta = "indep")) 
	list(bm=bm,brownie=bm2, ou=ou, ouch=t2, alphas=a2)
})
```




Reformat and label data for plotting



```r
names(fits) <- traits  # each fit is a different trait (so use it for a label)
data <- melt(fits)
names(data) <- c("regimes", "param", "rep", "value", "model", "trait")
```





```r
subdat <- subset(data, param %in% c("alpha") 
#                 & trait %in% c("kt", "open") 
                 & model %in% c("alphas") 
                 & value < 20)
r <- cast(subdat, regimes ~ model ~ trait ~ param, smedian.hilow, conf.int=.5, na.rm=T)
upper <- sapply(c("alpha"), function(t) max(r[, , , t]))
```






```r
p4 <-  ggplot(subdat, aes(model, value, fill=regimes)) + 
#  stat_summary(fun.y=mean, geom="bar", position="dodge", alpha=.5) + # add bars for some extra ink...
  stat_summary(fun.data=median_hilow, geom="pointrange", aes(color=regimes), 
  position = position_dodge(width=0.90), conf.int=.5) +
  facet_grid(param ~ trait, scales = "free_y") + 
  coord_cartesian(ylim=c(0,upper["alpha"]), wise=TRUE) +
  opts(title = "alpha")
```










