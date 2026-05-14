#include <bits/stdc++.h>
#include <chrono>   // wall-clock time  (std::chrono::steady_clock)
#include <ctime>    // CPU time         (std::clock)
#include <iomanip>  // std::setprecision / std::fixed
using namespace std;

// ============================================================
// TIMER — medicao de tempo no estilo CPLEX
//
// CPLEX reporta dois tempos separados:
//   - "Elapsed time"  : tempo de parede (wall clock), medido com
//                       um relogio monotônico que nunca retrocede
//                       mesmo que o horario do sistema mude.
//                       Aqui: std::chrono::steady_clock.
//   - "CPU time"      : tempo de processador consumido pelo
//                       processo (todos os threads somados).
//                       Aqui: std::clock() / CLOCKS_PER_SEC.
//
// Uso:
//   Timer t;
//   t.start();
//   ... algoritmo ...
//   cout << t.elapsedWall() << " s (wall)\n";
//   cout << t.elapsedCPU()  << " s (CPU)\n";
// ============================================================
struct Timer
{
    using Clock     = chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint wallStart;
    clock_t   cpuStart;
    bool      running = false;

    void start()
    {
        wallStart = Clock::now();
        cpuStart  = clock();
        running   = true;
    }

    // Segundos de parede decorridos desde start()
    double elapsedWall() const
    {
        auto now = Clock::now();
        return chrono::duration<double>(now - wallStart).count();
    }

    // Segundos de CPU consumidos desde start()
    double elapsedCPU() const
    {
        return static_cast<double>(clock() - cpuStart) / CLOCKS_PER_SEC;
    }
};

// Timer global — disponivel em todo o programa (inclusive dentro
// de search/searchPartition para registrar quando cada solucao
// foi encontrada, como o log de incumbentes do CPLEX).
Timer gTimer;

// ============================================================

const string nome_do_arquivo = "D:/tcc_algoritmo/comparacoes/instancia_exemplo3.txt";
const int MAXNODE = 10000;
const int MAXCOL  = 200;

int leftN[MAXNODE], rightN[MAXNODE];
int upN[MAXNODE],   downN[MAXNODE];
int rowID[MAXNODE];
int colID[MAXNODE];
int colSize[MAXCOL];

int header    = 0;
int solution[100];
int solSize   = 0;
int nodeCount = 0;
int nCols;

int  rowCost[MAXNODE];
int  bestCost    = INT_MAX;
int  bestSol[100];
int  bestSolSize = 0;
bool usedInBranch[MAXNODE];

int totalNos   = 0;
int totalPodas = 0;

// ============================================================
// UTILITARIOS DE IMPRESSAO
// ============================================================

string ind(int k) { return string(k * 3, ' '); }

void printMatriz(int k, bool showUsed)
{
    vector<int> cols;
    for (int c = rightN[header]; c != header; c = rightN[c])
        cols.push_back(c);

    if (cols.empty())
    {
        cout << ind(k) << "  (todas as colunas cobertas)\n";
        return;
    }

    set<int> linhas;
    for (int c : cols)
        for (int nd = downN[c]; nd != c; nd = downN[nd])
            linhas.insert(rowID[nd]);

    string pad = ind(k) + "  ";

    cout << pad << "Linha | ";
    for (int c : cols) cout << "C" << c << "  ";
    cout << "| Custo\n";
    cout << pad << "------|";
    for (int i = 0; i < (int)cols.size(); i++) cout << "----";
    cout << "|-------\n";

    for (int r : linhas)
    {
        bool usado = showUsed && usedInBranch[r];
        set<int> colsDaLinha;
        for (int c : cols)
            for (int nd = downN[c]; nd != c; nd = downN[nd])
                if (rowID[nd] == r)
                    colsDaLinha.insert(c);

        cout << pad << "  L" << r << (usado ? "*" : " ") << "  | ";
        for (int c : cols)
            cout << (colsDaLinha.count(c) ? "1   " : ".   ");
        cout << "| " << rowCost[r];
        if (usado) cout << "  (ja usada)";
        cout << "\n";
    }
    if (showUsed)
        cout << pad << "  (* = bloqueada por usedInBranch)\n";
}

void printCustoAtual(int k, int custoAtual)
{
    cout << ind(k) << "  Custo acumulado : " << custoAtual << "\n";
    if (bestCost == INT_MAX)
        cout << ind(k) << "  bestCost        : (nenhuma solucao ainda)\n";
    else
    {
        cout << ind(k) << "  bestCost        : " << bestCost << "\n";
        cout << ind(k) << "  Margem restante : " << bestCost - custoAtual << "\n";
    }
}

// Imprime a melhor solucao encontrada, incluindo o instante em
// que ela foi encontrada — identico ao log de incumbentes do CPLEX.
void printMelhorSolucao()
{
    // Captura o tempo ANTES de qualquer I/O para nao inflar a medicao
    double tWall = gTimer.elapsedWall();
    double tCPU  = gTimer.elapsedCPU();

    cout << "\n  +==============================================+\n";
    cout << "  |      NOVA MELHOR SOLUCAO ENCONTRADA        |\n";
    cout << "  +==============================================+\n";

    // --- Timestamp no estilo CPLEX: "Solution time = X.XX sec" ---
    cout << fixed << setprecision(4);
    cout << "  Tempo de parede (wall) : " << tWall << " s\n";
    cout << "  Tempo de CPU           : " << tCPU  << " s\n";
    cout << defaultfloat;                    // restaura formatacao padrao
    // ------------------------------------------------------------

    int total = 0;
    for (int i = 0; i < solSize; i++)
    {
        int r = rowID[solution[i]];
        cout << "    Linha " << r << " | custo = " << rowCost[r] << "\n";
        total += rowCost[r];
    }
    cout << "  Custo total: " << total;
    if (bestCost != INT_MAX)
        cout << "  (melhora de " << bestCost - total << " em relacao ao anterior)";
    else
        cout << "  (primeira solucao)";
    cout << "\n";

    cout << "\n  Cobertura:\n";
    cout << "  Linha | ";
    for (int c = 1; c <= nCols; c++) cout << "C" << c << "  ";
    cout << "| Custo\n";
    cout << "  ------|";
    for (int c = 1; c <= nCols; c++) cout << "----";
    cout << "|-------\n";
    for (int i = 0; i < solSize; i++)
    {
        int noBase = solution[i];
        int r      = rowID[noBase];
        set<int> cobertos;
        cobertos.insert(colID[noBase]);
        for (int j = rightN[noBase]; j != noBase; j = rightN[j])
            cobertos.insert(colID[j]);
        cout << "    L" << r << "  | ";
        for (int c = 1; c <= nCols; c++)
            cout << (cobertos.count(c) ? "1   " : ".   ");
        cout << "| " << rowCost[r] << "\n";
    }
    cout << "  +==============================================+\n\n";
}

// ============================================================
// cover / uncover  — apenas lista horizontal de headers
// ============================================================
void cover(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];
}

void uncover(int c)
{
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
}

// ============================================================
// coverFull / uncoverFull — DLX completo
// ============================================================
void coverFull(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];

    for (int r = downN[c]; r != c; r = downN[r])
    {
        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            upN[downN[j]] = upN[j];
            downN[upN[j]] = downN[j];
            colSize[colID[j]]--;
        }
    }
}

void uncoverFull(int c)
{
    for (int r = upN[c]; r != c; r = upN[r])
    {
        for (int j = leftN[r]; j != r; j = leftN[j])
        {
            colSize[colID[j]]++;
            upN[downN[j]] = j;
            downN[upN[j]] = j;
        }
    }
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
}

int chooseColumn()
{
    int best = rightN[header];
    for (int c = rightN[header]; c != header; c = rightN[c])
        if (colSize[c] < colSize[best])
            best = c;
    return best;
}

bool colunaAtiva(int c)
{
    for (int x = rightN[header]; x != header; x = rightN[x])
        if (x == c) return true;
    return false;
}

// ============================================================
// search() — Set Cover
// ============================================================
void search(int k, int MAX_K, int custoAtual)
{
    totalNos++;

    if (custoAtual >= bestCost)
    {
        totalPodas++;
        cout << ind(k) << "[PODA] custo=" << custoAtual
             << " >= bestCost=" << bestCost << ". Backtrack.\n";
        return;
    }

    if (rightN[header] == header)
    {
        printMelhorSolucao();
        bestCost    = custoAtual;
        bestSolSize = solSize;
        for (int i = 0; i < solSize; i++) bestSol[i] = solution[i];
        return;
    }

    if (k >= MAX_K)
    {
        cout << ind(k) << "[PODA] profundidade maxima. Backtrack.\n";
        return;
    }

    int c = chooseColumn();

    cout << "\n" << ind(k) << "=== NIVEL " << k << " [SET COVER] ===\n";
    printCustoAtual(k, custoAtual);
    cout << ind(k) << "  Coluna escolhida: C" << c
         << " (colSize=" << colSize[c] << ")\n\n";
    printMatriz(k, true);
    cout << "\n";

    cover(c);
    colSize[c]--;

    for (int r = downN[c]; r != c; r = downN[r])
    {
        if (usedInBranch[rowID[r]])
        {
            cout << ind(k) << "  L" << rowID[r] << " ignorada (usedInBranch)\n";
            continue;
        }

        int novoCusto = custoAtual + rowCost[rowID[r]];

        cout << ind(k) << "  --> Tentando L" << rowID[r]
             << " | custo=" << rowCost[rowID[r]]
             << " | acumulado ficaria=" << novoCusto
             << " | bestCost=" << (bestCost == INT_MAX ? -1 : bestCost) << "\n";

        if (novoCusto >= bestCost)
        {
            totalPodas++;
            cout << ind(k) << "      [PODA ANTECIPADA] "
                 << novoCusto << " >= " << bestCost << ". Nem desce.\n";
            continue;
        }

        usedInBranch[rowID[r]] = true;
        solution[solSize++]    = r;

        vector<int> cobertas;
        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            if (!colunaAtiva(colID[j]))
            {
                cout << ind(k) << "      C" << colID[j] << " ja coberta, pulando\n";
                continue;
            }
            colSize[colID[j]]--;
            cover(colID[j]);
            cobertas.push_back(colID[j]);
            cout << ind(k) << "      Cobrindo C" << colID[j]
                 << " (colSize agora=" << colSize[colID[j]] << ")\n";
        }

        search(k + 1, MAX_K, novoCusto);

        for (int i = (int)cobertas.size() - 1; i >= 0; i--)
        {
            uncover(cobertas[i]);
            colSize[cobertas[i]]++;
            cout << ind(k) << "      Restaurando C" << cobertas[i]
                 << " (colSize volta=" << colSize[cobertas[i]] << ")\n";
        }

        solSize--;
        usedInBranch[rowID[r]] = false;
        cout << ind(k) << "  <-- Desfez L" << rowID[r] << "\n";
    }

    colSize[c]++;
    uncover(c);
    cout << ind(k) << "  Restaurou C" << c << ". Backtrack nivel " << k << ".\n";
}

// ============================================================
// searchPartition() — Set Partition (cobertura exata)
// ============================================================
void searchPartition(int k, int MAX_K, int custoAtual)
{
    totalNos++;

    if (custoAtual >= bestCost)
    {
        totalPodas++;
        cout << ind(k) << "[PODA] custo=" << custoAtual
             << " >= bestCost=" << bestCost << ". Backtrack.\n";
        return;
    }

    if (rightN[header] == header)
    {
        printMelhorSolucao();
        bestCost    = custoAtual;
        bestSolSize = solSize;
        for (int i = 0; i < solSize; i++) bestSol[i] = solution[i];
        return;
    }

    if (k >= MAX_K)
    {
        cout << ind(k) << "[PODA] profundidade maxima. Backtrack.\n";
        return;
    }

    int c = chooseColumn();

    if (colSize[c] == 0)
    {
        cout << ind(k) << "[PODA] C" << c
             << " nao pode ser coberta (colSize=0). Backtrack.\n";
        return;
    }

    cout << "\n" << ind(k) << "=== NIVEL " << k << " [SET PARTITION] ===\n";
    printCustoAtual(k, custoAtual);
    cout << ind(k) << "  Coluna escolhida: C" << c
         << " (colSize=" << colSize[c] << ")\n\n";
    printMatriz(k, false);
    cout << "\n";

    coverFull(c);

    for (int r = downN[c]; r != c; r = downN[r])
    {
        int novoCusto = custoAtual + rowCost[rowID[r]];

        cout << ind(k) << "  --> Tentando L" << rowID[r]
             << " | custo=" << rowCost[rowID[r]]
             << " | acumulado ficaria=" << novoCusto
             << " | bestCost=" << (bestCost == INT_MAX ? -1 : bestCost) << "\n";

        if (novoCusto >= bestCost)
        {
            totalPodas++;
            cout << ind(k) << "      [PODA ANTECIPADA] "
                 << novoCusto << " >= " << bestCost << ". Nem desce.\n";
            continue;
        }

        solution[solSize++] = r;

        vector<int> cobertas;
        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            coverFull(colID[j]);
            cobertas.push_back(colID[j]);
            cout << ind(k) << "      Cobrindo C" << colID[j]
                 << " (exata, colSize agora=" << colSize[colID[j]] << ")\n";
        }

        searchPartition(k + 1, MAX_K, novoCusto);

        for (int i = (int)cobertas.size() - 1; i >= 0; i--)
        {
            uncoverFull(cobertas[i]);
            cout << ind(k) << "      Restaurando C" << cobertas[i]
                 << " (colSize volta=" << colSize[cobertas[i]] << ")\n";
        }

        solSize--;
        cout << ind(k) << "  <-- Desfez L" << rowID[r] << "\n";
    }

    uncoverFull(c);
    cout << ind(k) << "  Restaurou C" << c << ". Backtrack nivel " << k << ".\n";
}

// ============================================================
// initDLX e addNode
// ============================================================
void initDLX(int cols)
{
    nCols = cols;
    for (int i = 0; i <= cols; i++)
    {
        leftN[i]  = i - 1;
        rightN[i] = i + 1;
        upN[i] = downN[i] = i;
        colSize[i] = 0;
    }
    leftN[0]     = cols;
    rightN[cols] = 0;
    nodeCount    = cols + 1;
    memset(usedInBranch, false, sizeof(usedInBranch));
}

void addNode(int r, int c)
{
    int node = nodeCount++;
    rowID[node] = r;
    colID[node] = c;
    colSize[c]++;

    downN[node]    = downN[c];
    upN[node]      = c;
    upN[downN[c]]  = node;
    downN[c]       = node;

    if (rowID[node - 1] != r)
    {
        leftN[node] = rightN[node] = node;
    }
    else
    {
        leftN[node]          = leftN[node - 1];
        rightN[node]         = node - 1;
        rightN[leftN[node]]  = node;
        leftN[node - 1]      = node;
    }
}

// ============================================================
// Impressao do resumo de tempo — estilo CPLEX Solution Summary
// ============================================================
void printTimingSummary(double tWall, double tCPU)
{   
    cout << nome_do_arquivo << "\n";
    cout << "\n";
    cout << "+-------------------------------------------------+\n";
    cout << "|            RESUMO DE TEMPO (CPLEX-style)        |\n";
    cout << "+-------------------------------------------------+\n";
    cout << fixed << setprecision(4);
    cout << "| Tempo de parede (Elapsed time) : " << setw(10) << tWall << " s |\n";
    cout << "| Tempo de CPU   (CPU time)      : " << setw(10) << tCPU  << " s |\n";
    cout << "+-------------------------------------------------+\n";
    cout << defaultfloat;
}

// ============================================================
// main
// ============================================================
int main()
{
    int modo = 0;
    while (modo != 1 && modo != 2)
    {
        cout << "Escolha o modo de busca:\n";
        cout << "  1 - Set Cover    (cada coluna coberta ao menos uma vez)\n";
        cout << "  2 - Set Partition (cada coluna coberta exatamente uma vez)\n";
        cout << "Opcao: ";
        cin >> modo;
        if (modo != 1 && modo != 2)
            cout << "Opcao invalida. Tente novamente.\n\n";
    }

    ifstream in(nome_do_arquivo);

    int m, n;
    in >> m >> n;

    initDLX(n);

    string line;
    getline(in, line);

    vector<vector<int>> mat(m + 1, vector<int>(n + 1));

    for (int i = 1; i <= m; i++)
    {
        getline(in, line);
        stringstream ss(line);

        vector<int> valores;
        int x;
        while (ss >> x) valores.push_back(x);

        bool temPeso = (valores.size() == (size_t)(n + 1));

        for (int j = 1; j <= n; j++)
        {
            mat[i][j] = valores[j - 1];
            if (mat[i][j] == 1)
                addNode(i, j);
        }

        rowCost[i] = temPeso ? valores[n] : 1;
    }

    cout << "\nInstancia carregada!\n";
    cout << "Linhas: " << m << " | Colunas: " << n << "\n";
    cout << "Modo   : " << (modo == 1 ? "Set Cover" : "Set Partition") << "\n\n";

    // -------------------------------------------------------
    // Inicia o timer IMEDIATAMENTE antes do algoritmo,
    // exatamente como o CPLEX inicia a contagem ao chamar
    // CPXXmipopt() / CPXXlpopt().
    // -------------------------------------------------------
    gTimer.start();

    if (modo == 1)
        search(0, n, 0);
    else
        searchPartition(0, n, 0);

    // Captura os dois tempos logo apos o algoritmo terminar
    double tWallTotal = gTimer.elapsedWall();
    double tCPUTotal  = gTimer.elapsedCPU();
    // -------------------------------------------------------

    cout << "\nResultado final:\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma solucao encontrada.\n";
    }
    else
    {
        cout << "Custo minimo: " << bestCost << "\n";
        cout << "Linhas escolhidas: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << "L" << rowID[bestSol[i]] << " ";
        cout << "\n";
    }

    cout << "\nEstatisticas:\n";
    cout << "  Nos explorados : " << totalNos   << "\n";
    cout << "  Podas          : " << totalPodas << "\n";

    // Imprime o resumo de tempo no estilo CPLEX
    printTimingSummary(tWallTotal, tCPUTotal);

    return 0;
}