# README — DLX vs CPLEX no Set Cover Problem

## 🚀 Descrição
Este projeto tem como objetivo comparar o desempenho e a eficiência de duas abordagens distintas aplicadas ao **Set Cover Problem (SCP)**:
* **CPLEX**: Solucionador comercial de alto desempenho para programação linear e inteira.
* **DLX (Dancing Links + Algorithm X)**: Técnica baseada em matrizes esparsas e retrocesso (backtracking), proposta por Donald Knuth.

---

## 📂 Estado Atual
O projeto conta com implementações iniciais que utilizam instâncias de pequeno porte para validação lógica, localizadas no diretório:

`instancias_manuais/`

### Finalidade das Instâncias Manuais:
* **Validação do Algoritmo**: Garantir que a lógica de cobertura esteja correta.
* **Teste de Cover/Uncover**: Verificar a integridade dos ponteiros nas listas duplamente ligadas do DLX.
* **Depuração**: Facilitar o rastreamento de erros no fluxo do Algorithm X.
* **Estrutura**: Estabelecer a arquitetura base do código fonte.

---

## 🧪 Testes e Benchmarks
Os testes de escala estão sendo realizados com instâncias reais da **OR-Library**, especificamente:
* **Instância Alvo**: `scp41`

### Resultados Preliminares:
| Ferramenta | Resultado | Observação |
| :--- | :--- | :--- |
| **CPLEX** | Resolvido | Desempenho extremamente rápido (baseline). |
| **DLX** | Em processamento | A implementação atual ainda não convergiu para esta instância. |

---

## 🎯 Objetivo Atual
O foco do  agora é a otimização do DLX. Para que o algoritmo consiga resolver a instância `scp41` e competir com métodos tradicionais, as seguintes melhorias estão sendo implementadas:

1.  **Branch and Bound**: Implementação de limites para podar ramos improváveis da árvore de busca.
2.  **Heurísticas de Seleção**: Melhorar a escolha da coluna para reduzir o fator de ramificação.
3.  **Redução do Espaço de Busca**: Pré-processamento para eliminar redundâncias na matriz de cobertura.

---

## 📚 Referências
* **Donald Knuth** — *Dancing Links* (Millennial Fashions).
* **Algorithm X**: O algoritmo de backtracking de Knuth.
* **OR-Library**: Repositório de instâncias para problemas de pesquisa operacional.
* **IBM ILOG CPLEX Optimization Studio**.
