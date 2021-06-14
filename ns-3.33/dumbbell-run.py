#!/usr/bin/env python
import time
import os
import subprocess ,signal
import argparse
def run_procedure(ns3_exe_path,inst,cc,cc1,folder,loss_str="0"):
    exe_cmd=ns3_exe_path+"tcp-dumbbell  --it=%s --cc1=%s --cc2=%s --folder=%s --lo=%s"
    cmd=exe_cmd%(inst,cc,cc1,folder,loss_str)
    process= subprocess.Popen(cmd,shell = True)
    while 1:
        time.sleep(1)
        ret=subprocess.Popen.poll(process)
        if ret is None:
            continue
        else:
            break
def run_algo_no_random_loss(ns3_exe_path):
    inst_table=["1","2","3","4","5","6","7","8"]
    cc_algos=["reno","cubic","bbr","bbr2"]
    for c in range(len(cc_algos)):
        cc=cc_algos[c]
        folder=cc+"-l0"
        for i in range(len(inst_table)):
            inst=inst_table[i]
            run_procedure(ns3_exe_path,inst,cc,cc,folder)
def run_algo_random_loss(ns3_exe_path):
    inst_table=["3","4","7","8"]
    cc_algos=["reno","cubic","bbr","bbr2"]
    loss_table=["10","20","30"]
    for c in range(len(cc_algos)):
        cc=cc_algos[c]
        for l in range(len(loss_table)):
            folder=cc+"-l"+loss_table[l]
            for i in range(len(inst_table)):
                inst=inst_table[i]
                run_procedure(ns3_exe_path,inst,cc,cc,folder,loss_table[l])
def run_algo_bandwidth_competence(ns3_exe_path):
    inst_table=["1","2","3","4"]
    cc1_algos=["bbr","bbr2"]
    cc2_algos=["reno","cubic"]
    for c1 in range(len(cc1_algos)):
        for c2 in range(len(cc2_algos)):
            cc1=cc1_algos[c1]
            cc2=cc2_algos[c2]
            folder=cc1+"-"+cc2
            for i in range(len(inst_table)):
                inst=inst_table[i]
                run_procedure(ns3_exe_path,inst,cc1,cc2,folder)
if __name__ == '__main__':
    ns3_root="/home/ipcom/zsy/bbr-ns3.33/ns-3.33/"
    ns3_exe_path=ns3_root+"build/scratch/"
    ns3_lib_path=ns3_root+"build/lib/"
    old = os.environ.get("LD_LIBRARY_PATH")
    if old:
        os.environ["LD_LIBRARY_PATH"] = old + ":" +ns3_lib_path
    else:
        os.environ["LD_LIBRARY_PATH"] = ns3_lib_path
    #run_algo_no_random_loss(ns3_exe_path);
    run_algo_random_loss(ns3_exe_path)
    run_algo_bandwidth_competence(ns3_exe_path)
