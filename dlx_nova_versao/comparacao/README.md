# Comparacao DLX x CPLEX

Esta pasta executa os dois solvers sob o mesmo protocolo e conserva tanto os dados brutos quanto um resumo por mediana.

## Uso rapido

No diretorio `dlx_nova_versao/comparacao`:

```bash
./benchmark.sh 600 3
```

Os argumentos sao, nessa ordem, limite de tempo do nucleo de solucao em segundos e numero de repeticoes. Sem nomes adicionais, todas as instancias locais de `../instancias` sao usadas. Para escolher algumas:

```bash
./benchmark.sh 60 5 scp41 scp42 scp43
./benchmark.sh 300 3 'scp5*'
```

Cada execucao cria uma pasta nova em `resultados/`. Os arquivos principais sao:

- `runs.csv`: uma linha por execucao, com tempos internos, tempo do processo, CPU, memoria, custo, status e caminho do log;
- `resumo.csv`: medianas por instancia e solver, validacao dos custos e speedup;
- `resumo.md`: tabela compacta para leitura;
- `metadata.txt`: compilador, flags, CPLEX, limites e instancias;
- `logs/`: saida integral e medicao externa de cada processo.

## Onde o tempo comeca

Ha duas visoes complementares:

1. `total_after_read_wall_s`, a metrica principal, comeca imediatamente depois que a leitura da instancia termina nos dois programas. Ela inclui toda a preparacao e todo o nucleo exato. O tempo e dividido em `preparation_wall_s` e `core_wall_s`, mas essa divisao e apenas diagnostica, pois os algoritmos organizam o presolve de maneiras diferentes.
2. `process_wall_s` comeca antes de iniciar o executavel e termina quando ele sai. Portanto inclui inicializacao do processo, carregamento de bibliotecas, licenca CPLEX, leitura, preparacao, solucao e impressao final.

O limite informado vale apenas para o nucleo (`search` no DLX e `solve` no CPLEX). Um limite externo com 15 segundos de tolerancia impede processos travados; ele pode ser ajustado por `HARD_GRACE_SECONDS`.

## Medidas de justica

- Os dois binarios usam `-O3 -DNDEBUG -std=c++17`.
- CPLEX usa uma thread, gap MIP zero e log interno desligado.
- DLX executa sequencialmente e nao imprime durante `search()`.
- Cada teste ocorre em um processo novo e nunca em paralelo com o outro solver.
- A ordem DLX/CPLEX alterna entre repeticoes e instancias para reduzir vies de cache e de ordem.
- `OMP_NUM_THREADS`, `OPENBLAS_NUM_THREADS` e `MKL_NUM_THREADS` sao fixados em 1.
- O resumo usa a mediana, menos sensivel a ruido que uma unica execucao.
- O benchmark nao baixa nem altera instancias.

`dlx_speedup_total_x` e calculado como mediana CPLEX / mediana DLX. Valor maior que 1 significa que o DLX foi mais rapido na metrica principal.

Para reduzir migracao entre nucleos, escolha um nucleo permitido pelo sistema:

```bash
CPU_CORE=3 ./benchmark.sh 600 5
```

Para reaproveitar os binarios ja compilados:

```bash
SKIP_BUILD=1 ./benchmark.sh 600 3
```

Para informar outra instalacao do CPLEX:

```bash
CPLEX_HOME=/caminho/CPLEX_Studio2211 ./benchmark.sh 600 3
```

Os wrappers abaixo usam a mesma infraestrutura, mas executam somente um solver:

```bash
./run_dlx_all.sh 600 3
./run_cplex_all.sh 600 3
```

Para guardar uma rodada em um diretorio especifico, use `RESULT_DIR=/caminho/desejado`.
