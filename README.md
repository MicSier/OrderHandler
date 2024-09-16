# 
Project contains client using event listener pattern to stream to file orders based on a dummy market data provided by server nammed MarketDataVendor.

Sadly I did not have much time last week to finish the project properly so some features are missing like unit tests, server being multithreaded like client, more general approach to strategy criterion checking and I also didn't test on linux despite trying to write it to be cross-platform.

THe project is self contained without dependencies so it should be easy to understand and build. I created it in VS, but I don't think it is necessary to provide building instructions on linux, since you probably intent to just look at the code anyway.
