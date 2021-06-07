# ns3-tcp-bbr
Implement TCP BBR in ns3.33
## Build
```
cd ns3.33  
./waf configure  
./waf build  
```
## Run
```
./waf --run "scratch/tcp-test"  
```
## Results
Traced data can be found under ns3-tcp-bbr/ns-3.33/traces/  
Sending rate got from max bandwidth filter.  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr.git/blob/main/ns-3.33/traces/bbr-max-bw.png)   
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr.git/blob/main/ns-3.33/traces/bbr-instant-rtt.png)  
Infligh packets:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr.git/blob/main/ns-3.33/traces/inflight.png)  



