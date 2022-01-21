#!/usr/bin/python3
######################################
# 2021 tony anderson tandersn@uw.edu # 
######################################
# Contrary to what the name implies,
# this script does not use 'dd' but 
# simply reads and writes chunks of 
# binary data. Every 'CheckInterval'
# forks itself and runs a leveladj 
# type block and adjusts the gain. 
# the actual time interval will be 
# sample rate / BUFSIZE * CheckInterval
# this version of the script uses the 
# cx card 'level' paramter to adjust gain
# the cx card has a lot of inherent noise
# so i consider this to be only experimental
# the use of an external amp and gain 
# control will probably provide better 
# results, unless you have one of the 
# 1 in 100 cards that have low noise. 

import time
import os
import sys
import datetime
import numpy as np
def read_in_chunks(file_object):
    global BUFSIZE
    while True:
        data = file_object.read(BUFSIZE)
        if not data:
            break
        yield data

def child16(data):
    syssyfsfile = open(r"/sys/module/cxadc/parameters/level","r")    
    GainLevel = int(syssyfsfile.read())
    syssyfsfile.close()
    data = bytes(data)         # convert from list of ints into a byte array
    mv = memoryview(data)      # convert to a memory view...
    mv = mv.cast('H')          # treat every 2-bytes as a 16-bit value
    NumSampOver = 0
    NumSampGood = 0
    HighestSamp = 0
    LowestSamp = 45555
    GainAction = "Maintaining Gain: "
    for i in range(0, len(mv), 16):
        OneOfBuff = mv[i]
        if OneOfBuff > HighestSamp:
            HighestSamp = OneOfBuff
        if OneOfBuff < LowestSamp:
            LowestSamp = OneOfBuff
        if NumSampOver < BUFSIZE / 50000:
        #if NumSampOver < BUFSIZE / 200000:   
            if OneOfBuff < 514 or OneOfBuff > 64900:
                NumSampOver += 1
        else:
            NumSampOver = 99999
            break
    if NumSampOver == 99999:
        #GainLevel -= 2 
        GainLevel -= 1
        if GainLevel < 0 :
            GainLevel = 0
        #GainAction = "Lowering Gain: Coarse " + str(GainLevel) + " "
        GainAction = "Lowering Gain: Fine " + str(GainLevel) + " "
    elif NumSampOver >= 20:
        GainLevel -= 1
        if GainLevel < 0 :
            GainLevel < 0
        GainAction = "Lowering Gain: Fine " + str(GainLevel) + " "
    syssyfsfile = open(r"/sys/module/cxadc/parameters/level","w")
    syssyfsfile.write(str(GainLevel))
    syssyfsfile.close()
    now = datetime.datetime.now()
    #print (now,":",GainAction,"Low:",LowestSamp," High:",HighestSamp," Clipped:",NumSampOver,"                  ",end='\r')
    print (now,":",GainAction,"Low:",LowestSamp," High:",HighestSamp," Clipped:",NumSampOver,"                  ")
    os._exit(0)

def child(data):
    syssyfsfile = open(r"/sys/module/cxadc/parameters/level","r")    
    GainLevel = int(syssyfsfile.read())
    syssyfsfile.close()
    NumSampOver = 0
    NumSampGood = 0
    OneOfBuff = 0
    HighestSamp = 0
    LowestSamp = 215 
    GainAction = "Maintaining Gain: "
    for OneOfBuff in data:
        if OneOfBuff > HighestSamp:
           HighestSamp = OneOfBuff
        if OneOfBuff < LowestSamp:
             LowestSamp = OneOfBuff
        if NumSampOver < BUFSIZE / 50000:
        #if NumSampOver < BUFSIZE / 200000: 
            if OneOfBuff < 8 or OneOfBuff >248:
                NumSampOver += 1
        else:
            NumSampOver = 99999
            break
    if NumSampOver == 99999:  
        #GainLevel -= 2
        GainLevel -= 1 
        if GainLevel < 0 :
            GainLevel = 0
        #GainAction = "Lowering Gain: Coarse " + str(GainLevel) + " "
        GainAction = "Lowering Gain: Fine " + str(GainLevel) + " "
    elif NumSampOver >= 20:  
        GainLevel -= 1
        if GainLevel < 0 :
            GainLevel < 0
        GainAction = "Lowering Gain: Fine " + str(GainLevel) + " "
    syssyfsfile = open(r"/sys/module/cxadc/parameters/level","w")    
    syssyfsfile.write(str(GainLevel))
    syssyfsfile.close()
    now = datetime.datetime.now()
    #print (now,":",GainAction,"Low:",LowestSamp," High:",HighestSamp," Clipped:",NumSampOver,"                  ",end='\r')
    print (now,":",GainAction,"Low:",LowestSamp," High:",HighestSamp," Clipped:",NumSampOver,"                  ")
    os._exit(0)
##########################################################################################################

name_in  = "/dev/cxadc0"
name_out = sys.argv[1]
CheckInterval = 150 #checking interval 125 = ~7sec@40msps
CountVar = -150
BUFSIZE   = 2097152
print ("start: now using buff divided by 50k instead of 200k")
#print ("start: back using buff divided by  200k")
syssyfsfile = open(r"/sys/module/cxadc/parameters/tenbit","r")
tenbit = int(syssyfsfile.read())
syssyfsfile.close()
val_compare = (2 ** ((tenbit+1)*8)) -1
print (val_compare,tenbit)
in_fh = open(name_in, "rb")
out_fh = open(name_out, "wb")
for piece in read_in_chunks(in_fh):
    out_fh.write(piece)
    CountVar +=1
    if CountVar > CheckInterval: 
        CountVar = 0
        newpid = os.fork()
        if newpid == 0:
            if tenbit == 1:
               child16(piece)
            if tenbit == 0:
               child(piece)
    
in_fh.close()
out_fh.close()
print ("Fin")
os._exit(0)
