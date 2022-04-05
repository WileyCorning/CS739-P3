
date +"%T.%6N" 

for i in {1..100}; do write(${i}, 4_KB); done

date +"%T.%6N" 


