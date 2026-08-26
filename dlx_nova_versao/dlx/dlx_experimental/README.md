# Experimentos isolados sobre o DLX V2 sem restart

Este diretório mantém cinco variações independentes do
`dlx_v2_sem_restart.cpp`. Cada arquivo acrescenta somente uma ideia à mesma
base, para que o efeito da funcionalidade possa ser medido sem misturar
otimizações. Nenhuma variante contém o mecanismo de restart.

O `dlx_v2.cpp` original não foi alterado. Neste diretório, o executável
`bin/dlx_v2` representa a base sem restart. Os executáveis são gravados em
`bin/`, que está ignorado pelo Git.

## Variantes

1. `dlx_fixing_local.cpp`: separa, no nó atual, os candidatos cujo lower bound
   do filho já não pode melhorar o incumbente. Essas linhas são proibidas em
   lote durante toda a subárvore e restauradas no backtracking.
2. `dlx_strong_branching.cpp`: considera as quatro colunas de menor
   cardinalidade e escolhe aquela com menos filhos sobreviventes segundo o
   lower bound. Empates preservam critérios baratos da V2.
3. `dlx_residual_bound.cpp`: acrescenta um bound de empacotamento de duas
   colunas. Se nenhuma linha disponível cobre simultaneamente as duas, soma os
   menores custos necessários para cobri-las com linhas distintas.
4. `dlx_candidate_ordering.cpp`: ordena primeiro os filhos mais próximos do
   corte pelo bound, depois por custo reduzido, custo da linha e cobertura.
5. `dlx_unit_propagation.cpp`: quando a coluna escolhida tem apenas um
   candidato disponível, seleciona essa linha diretamente, sem criar,
   ordenar ou percorrer uma lista de irmãos inexistentes.

## Compilação

```bash
cd dlx_nova_versao/dlx/dlx_experimental
make -j2
```

Isso compila a base e as cinco variantes com `-O3 -DNDEBUG -std=c++17`.

## Execução

O último argumento é o limite interno da busca, em segundos. Exemplo com 15
minutos e afinidade a um único núcleo:

```bash
taskset -c 2 ./bin/dlx_strong_branching ../../instancias_300_3000/scpa3.txt 900
```

Para executar a base e todas as variantes, sem criar outro script `.sh`:

```bash
for solver in dlx_v2 dlx_fixing_local dlx_strong_branching dlx_residual_bound dlx_candidate_ordering dlx_unit_propagation; do
    taskset -c 2 "./bin/$solver" ../../instancias_300_3000/scpa3.txt 900
done
```

## Benchmark das cinco variantes

O runner abaixo compila os executáveis, roda `scpa1` até `scpa5` e gera uma
tabela com status, custo, número de nós e medianas de tempo. O limite informado
é aplicado ao `search` de cada execução; existe ainda uma tolerância externa de
60 segundos para preparação e encerramento.

Uma execução por combinação, limite de 100 segundos e afinidade ao núcleo 2:

```bash
./benchmark_variants.py 100 --core 2
```

Isso executa exatamente as cinco variantes. Para incluir também a base DLX V2
sem restart:

```bash
./benchmark_variants.py 100 --core 2 --include-baseline
```

Para obter medianas mais confiáveis, por exemplo com três repetições:

```bash
./benchmark_variants.py 100 --core 2 --repetitions 3 --include-baseline
```

O progresso aparece no terminal. Ao final, a tabela também é salva em
`resultados/<data_hora>/resumo.md`, os dados individuais em `runs.csv` e a
saída completa de cada processo em arquivos `.log`. Todo o diretório de
resultados está ignorado pelo Git.

## Triagem realizada

Todos os casos abaixo terminaram com status `Optimal` e o mesmo custo da base.
Os tempos são apenas da busca; preparação e heurísticas são comuns às
variantes. Foi feita uma execução por combinação, portanto estes números são
triagem, não uma conclusão estatística.

| Variante | scpa4: tempo / nós | scpa5: tempo / nós | scpa3: tempo / nós |
|---|---:|---:|---:|
| DLX V2 sem restart | 0,2380 s / 334.199 | 0,0243 s / 41.206 | 40,8729 s / 57.445.267 |
| Fixing local | 0,1534 s / 209.948 | 0,0229 s / 34.122 | 31,6249 s / 46.960.786 |
| Strong branching | 0,2116 s / 113.369 | 0,0185 s / 10.996 | 26,2085 s / 14.935.291 |
| Bound residual | 0,2990 s / 334.199 | 0,0341 s / 41.206 | 48,0445 s / 57.445.258 |
| Ordenação | 0,5188 s / 807.599 | 0,0653 s / 119.299 | 34,5125 s / 63.576.948 |
| Candidato único | 0,2333 s / 334.199 | 0,0289 s / 41.206 | 35,7520 s / 57.445.267 |

Os testes de `scpa4` e `scpa5` acima foram repetidos depois da remoção do
restart. Os números de `scpa3` permanecem da triagem anterior: nela nenhuma
variante melhorou o UB durante o search e nenhum restart foi acionado, portanto
a árvore medida já corresponde ao comportamento sem restart.

Na `scpa3`, o strong branching reduziu aproximadamente 74% dos nós e 36% do
tempo de busca. O fixing local reduziu aproximadamente 18% dos nós e 23% do
tempo. Em `scpa4`, o fixing local reduziu aproximadamente 37% dos nós e 36% do
tempo; em `scpa5`, o strong branching reduziu aproximadamente 73% dos nós e 24%
do tempo.

O bound residual atual quase não poda e acrescenta uma varredura, então não é
promissor nesta forma. A nova ordenação foi instável: piorou muito `scpa4` e
`scpa5`, embora tenha reduzido o tempo da `scpa3`; ela não deve substituir a
ordenação da V2 sem uma amostra maior.

## Validações técnicas

- Compilação normal de todos os executáveis pelo `Makefile`.
- Compilação dos cinco experimentos com `-Wall -Wextra -Wpedantic`, sem
  avisos.
- Execução instrumentada com UBSan em `scpa5`, sem comportamento indefinido
  detectado; todas as variantes retornaram `Optimal`, custo 236.
- Verificação funcional em `scpa4`, `scpa5` e `scpa3`, sempre com o mesmo
  custo ótimo da base.
