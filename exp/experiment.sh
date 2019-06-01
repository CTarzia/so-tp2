#/bin/bash


TP=../codigo/blockchain

make -C $(dirname $TP)

# echo "dificultad,nodos,tiempo" > results.csv

for DIFFICULTY in  9 11 13 16
do
    for nodes in 4 32
    do
        for repeat in {1..3}
        do
            DIFFICULTY=$DIFFICULTY /usr/bin/time -o outfile -f "%e" mpirun -np $nodes $TP >/dev/null
            TIME=`cat outfile`
            echo "$DIFFICULTY,$nodes,$TIME" >> results.csv
            echo "$DIFFICULTY,$nodes,$TIME"
        done
    done
    echo "#### Fin de dificultad: $DIFFICULTY ####"
done


# DIFFICULTY=15

# echo "validacion,nodos,tiempo" > results2.csv
# for repeat in {1..10}
# do
#     for VALIDATION in 1 2 3 5 10 15
#     do
#         for nodes in 4 8 16 32 64
#         do
#             VALIDATION=$VALIDATION DIFFICULTY=$DIFFICULTY /usr/bin/time -o outfile -f "%e" mpirun -np $nodes $TP >/dev/null
#             TIME=`cat outfile`
#             echo "$VALIDATION,$nodes,$TIME" >> results2.csv
#             echo "$VALIDATION,$nodes,$TIME"
#         done
#     done
# done

echo "finish"
