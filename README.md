# ns3-tcp-bbr
Implement TCP BBR in ns3.33
## Build
```
cd ns3.33  
./waf configure  
./waf build  
```
## Run bbr  
```
./waf --run "scratch/tcp-test --cc=bbr --folder=bbr"   
```
## Results
Traced data can be found under ns3-tcp-bbr/ns-3.33/traces/  
Sending rate got from max bandwidth filter.  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/bbr-max-bw.png)   
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/bbr-instant-rtt.png)  
Inflight packets:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/inflight.png)  
## Run bbr2  
```
./waf --run "scratch/tcp-test --cc=bbr2 --folder=bbr2"   
```
## Results
Traced data can be found under ns3-tcp-bbr/ns-3.33/traces/  
Sending rate got from max bandwidth filter.  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/bbr2-max-bw.png)   
Instant rtt values:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/bbr2-instant-rtt.png)  
Inflight packets:  
![avatar](https://github.com/SoonyangZhang/ns3-tcp-bbr/blob/bbrv2/ns-3.33/traces/bbr2-inflight.png)  
## Notice
To implement bbr2, MarkSkbLost is added in TcpCongestionOps.  
```
virtual void MarkSkbLost(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,  
                     const TcpTxItem *skb);   
```
ECN is not test, and the reaction to CA_EVENT_ECN_IS_CE should be added in CwndEvent (refer to tcp-dctcp.cc).  
## More
A dummbell topology is given in scratch/tcp-dumbbell.cc. And a python script is given in dumbbell-run.py to run tcp-dumbbell.  
```
python dumbbell-run.py  
```
And the variable ns3_root in dumbbell-run.py should be configured correctly.  



