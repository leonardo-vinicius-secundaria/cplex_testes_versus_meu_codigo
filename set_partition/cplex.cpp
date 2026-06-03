/*
 * CPLEX Solver — Set Partition (Exact Cover) for OR-Library scp41.txt
 *
 * Build (adjust paths to your CPLEX installation):
 *   g++ -O2 -std=c++17 scp41_set_partition_cplex.cpp \
 *       -I/opt/ibm/ILOG/CPLEX_Studio2211/cplex/include \
 *       -L/opt/ibm/ILOG/CPLEX_Studio2211/cplex/lib/x86-64_linux/static_pic \
 *       -lcplex -lm -lpthread -ldl \
 *       -o scp41_cplex
 *
 * Run:
 *   ./scp41_cplex scp41.txt
 *
 * OR-Library scp format:
 *   Line 1  : nrows ncols
 *   Next    : ncols costs, possibly spanning multiple lines
 *   Then, for each row i = 0..nrows-1:
 *     k_i
 *     s1 s2 ... sk_i   (1-indexed columns that cover row i)
 *
 * Set Partition model (Exact Cover):
 *   min  sum_j  c_j * x_j
 *   s.t. sum_{j : a_ij=1} x_j  = 1   for all rows i      [each row covered EXACTLY once]
 *        x_j in {0,1}
 */

#include <ilcplex/ilocplex.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <numeric>

ILOSTLBEGIN

/* ─── helpers ────────────────────────────────────────────────────────────── */

static bool readToken(std::istream& in, int& val) {
    return static_cast<bool>(in >> val);
}

/* Read OR-Library scp instance.
   Returns false on failure.
   cost[j]    = cost of column j  (0-indexed)
   cover[j]   = set of rows (0-indexed) covered by column j
*/
bool readSCP(const std::string& path,
             int& nrows, int& ncols,
             std::vector<double>& cost,
             std::vector<std::vector<int>>& cover)
{
    std::ifstream fin(path);
    if (!fin) { std::cerr << "Cannot open " << path << "\n"; return false; }

    if (!(fin >> nrows >> ncols)) { std::cerr << "Bad header\n"; return false; }
    if (nrows <= 0 || ncols <= 0) {
        std::cerr << "Bad dimensions: rows=" << nrows << " cols=" << ncols << "\n";
        return false;
    }

    cost.assign(ncols, 0.0);
    cover.assign(ncols, {});

    // Correct OR-Library SCP layout: all column costs come first.
    for (int j = 0; j < ncols; ++j) {
        int c;
        if (!readToken(fin, c)) {
            std::cerr << "Unexpected end of file while reading cost of column "
                      << (j + 1) << "\n";
            return false;
        }
        cost[j] = c;
    }

    // Then each row lists the columns that cover it.
    for (int i = 0; i < nrows; ++i) {
        int k;
        if (!readToken(fin, k)) {
            std::cerr << "Unexpected end of file while reading coverage count of row "
                      << (i + 1) << "\n";
            return false;
        }
        if (k < 0) {
            std::cerr << "Invalid negative coverage count in row " << (i + 1) << "\n";
            return false;
        }
        for (int t = 0; t < k; ++t) {
            int col;
            if (!readToken(fin, col)) {
                std::cerr << "Unexpected end of file while reading column "
                          << (t + 1) << " of row " << (i + 1) << "\n";
                return false;
            }
            if (col < 1 || col > ncols) {
                std::cerr << "Column index out of range in row " << (i + 1)
                          << ": " << col << " (valid range: 1.." << ncols << ")\n";
                return false;
            }
            cover[col - 1].push_back(i);
        }
    }

    return true;
}

/* ─── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char* argv[])
{
    std::string instPath = (argc > 1) ? argv[1] : "scp41.txt";
    double timeLimit = (argc > 2) ? std::stod(argv[2]) : 3600.0;

    int nrows, ncols;
    std::vector<double> cost;
    std::vector<std::vector<int>> cover;

    std::cout << "Reading instance: " << instPath << "\n";
    if (!readSCP(instPath, nrows, ncols, cost, cover)) return 1;
    std::cout << "  rows=" << nrows << "  cols=" << ncols << "\n\n";

    /* ── sanity: verify every row appears at least once ─────────────────── */
    std::vector<int> rowDeg(nrows, 0);
    for (int j = 0; j < ncols; ++j)
        for (int r : cover[j]) {
            if (r < 0 || r >= nrows) {
                std::cerr << "Internal parser error: row index out of range: " << r << "\n";
                return 1;
            }
            ++rowDeg[r];
        }
    int uncoverable = 0;
    for (int i = 0; i < nrows; ++i)
        if (rowDeg[i] == 0) ++uncoverable;
    if (uncoverable) {
        std::cerr << "WARNING: " << uncoverable
                  << " row(s) are not covered by any column — "
                  << "instance is infeasible as Set Partition.\n";
    }

    /* ── build CPLEX model ───────────────────────────────────────────────── */
    IloEnv   env;
    IloModel model(env);

    // Binary variables x[j]
    IloNumVarArray x(env, ncols, 0.0, 1.0, ILOBOOL);
    for (int j = 0; j < ncols; ++j) {
        std::string name = "x_" + std::to_string(j+1);
        x[j].setName(name.c_str());
    }

    // Objective: minimise total cost
    IloExpr obj(env);
    for (int j = 0; j < ncols; ++j) obj += cost[j] * x[j];
    model.add(IloMinimize(env, obj));
    obj.end();

    // Exact-cover constraints: for each row i, sum of covering columns == 1
    // Build row → covering columns index first (faster constraint generation)
    std::vector<std::vector<int>> rowCols(nrows);
    for (int j = 0; j < ncols; ++j)
        for (int r : cover[j]) rowCols[r].push_back(j);

    IloRangeArray cons(env);
    for (int i = 0; i < nrows; ++i) {
        IloExpr lhs(env);
        for (int j : rowCols[i]) lhs += x[j];
        std::string cname = "cov_" + std::to_string(i+1);
        cons.add(IloRange(env, 1.0, lhs, 1.0, cname.c_str()));
        lhs.end();
    }
    model.add(cons);

    /* ── CPLEX parameters ────────────────────────────────────────────────── */
    IloCplex cplex(model);

    // Keep CPLEX progress visible in the terminal.
    cplex.setParam(IloCplex::Param::MIP::Display, 2);
    cplex.setParam(IloCplex::Param::Simplex::Display, 0);

    // Prove optimality when possible.
    cplex.setParam(IloCplex::Param::MIP::Tolerances::MIPGap, 0.0);

    // Time limit in seconds (adjust as needed)
    cplex.setParam(IloCplex::Param::TimeLimit, timeLimit);

    // Use all available threads
    cplex.setParam(IloCplex::Param::Threads, 0);

    // Emphasise optimality proof (branch-and-cut)
    cplex.setParam(IloCplex::Param::MIP::Strategy::Search,
                   IloCplex::Traditional);

    // Enable strong branching for tighter bounds
    cplex.setParam(IloCplex::Param::MIP::Strategy::VariableSelect,
                   CPX_VARSEL_STRONG);

    // Cutting planes: aggressive
    cplex.setParam(IloCplex::Param::MIP::Cuts::Covers, 2);
    cplex.setParam(IloCplex::Param::MIP::Cuts::Cliques, 2);

    /* ── solve ───────────────────────────────────────────────────────────── */
    std::cout << "Solving Set Partition (Exact Cover) with CPLEX...\n";
    std::cout << "Time limit: " << timeLimit << " s\n\n";

    auto t0 = std::chrono::high_resolution_clock::now();
    bool solved = cplex.solve();
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "\n==================================================\n";
    std::cout << "Elapsed wall-clock time : " << elapsed << " s\n";
    std::cout << "CPLEX status            : " << cplex.getStatus() << "\n";

    if (!solved) {
        std::cout << "No solution found (infeasible or time limit).\n";
        env.end();
        return 0;
    }

    /* ── extract & display solution ─────────────────────────────────────── */
    double objVal = cplex.getObjValue();
    double bestBound = cplex.getBestObjValue();
    double gap = (objVal > 1e-9)
                 ? std::abs(objVal - bestBound) / std::abs(objVal) * 100.0
                 : 0.0;

    std::cout << "Objective (total cost)  : " << objVal << "\n";
    std::cout << "Best bound              : " << bestBound << "\n";
    std::cout << "MIP gap                 : " << gap << " %\n";

    IloNumArray xval(env, ncols);
    cplex.getValues(xval, x);

    std::vector<int> selected;
    for (int j = 0; j < ncols; ++j)
        if (xval[j] > 0.5) selected.push_back(j);

    std::cout << "\nSelected subsets (" << selected.size() << " columns):\n";
    for (int j : selected)
        std::cout << "  col " << (j+1) << "  cost=" << (int)cost[j]
                  << "  covers " << cover[j].size() << " rows\n";

    /* ── verify exact cover ──────────────────────────────────────────────── */
    std::vector<int> covered(nrows, 0);
    for (int j : selected)
        for (int r : cover[j]) ++covered[r];

    int ok = 0, bad = 0;
    for (int i = 0; i < nrows; ++i) {
        if (covered[i] == 1) ++ok;
        else                 ++bad;
    }
    std::cout << "\nVerification:\n";
    std::cout << "  Rows covered exactly once : " << ok  << "\n";
    std::cout << "  Rows NOT covered exactly  : " << bad << "\n";
    if (bad == 0)
        std::cout << "  ✓ Valid exact cover!\n";
    else
        std::cout << "  ✗ Solution is NOT a valid exact cover.\n";

    xval.end();
    env.end();
    return 0;
}
