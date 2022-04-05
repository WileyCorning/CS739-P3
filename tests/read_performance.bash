echo "read performance for 100 reads"
date +"%T.%6N" 
for i in {1..100}; do cat 4_KB > /dev/null ; done
date +"%T.%6N"
