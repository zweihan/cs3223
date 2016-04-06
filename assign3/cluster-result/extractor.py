import sys
import string
import matplotlib.pyplot as plt
numFles = len(sys.argv) - 1
files = []
for i in range(0, len(sys.argv)):
    if i == 0:
        continue
    files.append(sys.argv[i])
for file in files:
    f = open(file, 'read')
    output =[]
    order=[]
    data=[[],[]]
    firstLoop = True
    for line in f:
        if string.find(line, 'ALGOCHANGE: ') != -1:
            if firstLoop:
                firstLoop = False
                order.append(line.split(': ')[1].rstrip())
            else:
                output.append(data)
                order.append(line.split(': ')[1].rstrip())
                data=[[],[]]
        if string.find(line, 'ENDOFFILE__: ') != -1:
            output.append(data)
        if string.find(line, 'PARAMS:') != -1:
            params = line.split(': ')[1].rstrip()
            if(file.find('3')!= -1):
                params = int(params)/1024
            if(file.find('1')!=-1):
                params = int(params)/1000

            data[0].append(int(params))
        if string.find(line, 'Execution time: ') != -1:
            val = line.split(': ')[1].rstrip()
            val = val.split(' ')[0]
            data[1].append(val)
    print output
    print order
    shapes = ['r|-', 'gx-', 'b*-', 'ms-']
    i=0
    plt.clf()
    for l in output:
        x=l[0]
        y=l[1]
        plt.plot(x,y, shapes[i])
        i+=1
    plt.ylabel('Execution time(ms)')
    if(file.find('1')!= -1):
        plt.title('Effect of Selectivity on Query ' + file[1])
        plt.xlabel('Selectivity of selection predicate(%)')
    if(file.find('2')!= -1):
        plt.title('Effect of join factor on Query '+ file[1])
        plt.xlabel('Join Factor')
    if(file.find('3')!= -1):
        plt.title('Effect of work mem parameter on Query '+ file[1])
        plt.xlabel('Quantity of work mem (MB)')

    plt.legend(order, fancybox=True, framealpha=0, numpoints=1)
    plt.savefig(file[0]+file[1]+'fig', facecolor='w', edgecolor='w', orientation='portrait')
