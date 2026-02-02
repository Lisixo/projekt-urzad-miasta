#!/bin/bash

if [ -z "$1" ]; then
    echo "Nie podano liczby petentów"
    echo "Użycie: $0 <liczba_petentów>"
    exit 1
fi

n=$1

cd ./bin
for (( i=1; i<=n; i++ ))
do
    ./petent &
done