# ns3-tcp-bbr
Implement TCP BBR in ns3.33.  
If you used this repo to test BBR and Copa in your research and is interested in citing it here's how you do it:  
```
@Misc{,  
    author = {SoonyangZhang},  
    title = {ns3-tcp-bbr},  
    year = {2021--},  
    url = "https://github.com/SoonyangZhang/ns3-tcp-bbr"
  }  
```
## Build
```
cd ns3.33  
./waf configure  
./waf build  
```
## Run
```
./waf --run "scratch/tcp-test --cc=bbr --folder=bbr"  
```
## Results
Traced data can be found under ns3-tcp-bbr/ns-3.33/traces/  
Sending rate got from max bandwidth filter.  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/bbr-max-bw.png)  
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/bbr-instant-rtt.png)  
Inflight packets:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/inflight.png)  
# copa is added
## Run
```
./waf --run "scratch/tcp-test --cc=copa --folder=copa"  
```
## Results
Different latency factor will lead different behavior:  
when m_deltaParam=0.05:  
Sending rate  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/copa-20-max-bw.png)  
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/copa-20-instant-rtt.png)  
when m_deltaParam=0.5:  
Sending rate  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/copa-2-max-bw.png)  
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/main/ns-3.33/traces/copa-2-instant-rtt.png)  

