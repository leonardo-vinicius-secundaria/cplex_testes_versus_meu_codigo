# Benchmark scpe* - OR-Library Set Cover

Esta pasta compara o solver DLX adaptado para Set Cover com a formulacao MIP/ILP resolvida pelo CPLEX nas instancias `scpe1` a `scpe5` da OR-Library.

As instancias foram baixadas de:

```text
https://people.brunel.ac.uk/~mastjjb/jeb/orlib/files/
```

## Compilar

```bash
g++ -O2 -std=c++17 dlx_scp.cpp -o dlx_scp
./build_cplex.sh
```

## Rodar benchmark

```bash
./benchmark.sh 10
```

O resultado principal fica em:

```text
RESULTADOS/benchmark.csv
```

Nesta pasta, o tempo do CPLEX conta a construcao do modelo mais o `solve()`, enquanto o DLX conta presolve, bounds e busca apos a leitura da instancia. Isso torna a comparacao mais adequada para instancias pequenas, onde overhead de montagem do modelo pode pesar.
