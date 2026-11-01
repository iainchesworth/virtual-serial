Network backends now close their socket when a connect fails, leaving a
visibly closed port that can be retried.
