/*
ASSUMPTIONS
* The following are treated as errors: (output #ERR)
    1) Cell formula references undefined cell.
    2) Cell formula references a cycle.
    3) Invalid postfix syntax.
    4) Division by zero.
* Undefined cells are not printed.
* Postfix results calculated as integers (floored).
* Column references must uppercase (e.g. A0).
*/

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

class Spreadsheet {
   public:
    void parse_input(std::string);
    void print_output();
    void clear()
    {
        cells.clear();
        dependencies.clear();
        max_col = 0;
        max_row = 0;
    }

   private:
    enum class CellState { Empty, Error };

    using CellValue = std::variant<int, CellState>;

    // Spreadsheet dimensions
    int max_col = 0;
    int max_row = 0;

    // Store cells
    std::unordered_map<int, std::unordered_map<int, CellValue>> cells;

    // Store dependencies. Maps to {formula, deps[]} pair
    // NB: We are storing downstream dependencies
    // i.e: if A0 -> A1, this means A1's formula contains A0
    std::unordered_map<std::string,
                       std::pair<std::string, std::vector<std::string>>>
        dependencies;

    bool contains_letter(const std::string&);
    void calculate_postfix(std::pair<int, int>, const std::string&);
    void parse_tokens(std::pair<int, int>, const std::string&);
    void resolve_dependencies();
    void resolve_references(const std::string&);
    std::vector<std::string> topological_sort_dependencies();
    bool topological_dfs_helper(const std::string&,
                                std::unordered_set<std::string>&,
                                std::unordered_set<std::string>&,
                                std::vector<std::string>&);
    int col_to_coord(const std::string&);
    std::string coord_to_col(int);
    std::string coords_to_address(const std::pair<int, int>&);
    std::pair<int, int> address_to_coords(const std::string&);
    bool is_letter_number_format(const std::string&);
    void print_dependencies();
    bool is_empty(const CellValue& cell);
    bool is_error(const CellValue& cell);
};

void Spreadsheet::parse_input(std::string file_name)
{
    std::ifstream file(file_name);
    std::string line;
    int row = 0;
    int col = 0;
    std::string col_s;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string cell;
        col = 0;
        while (std::getline(ss, cell, ',')) {
            parse_tokens({col, row}, cell);
            ++col;
        }
        max_col = std::max(max_col, col - 1);
        ++row;
    }
    resolve_dependencies();
    max_row = row - 1;
}

void Spreadsheet::print_output()
{
    // Print column headers
    std::cout << "\t";
    for (int col = 0; col <= max_col; ++col) {
        std::cout << coord_to_col(col) << "\t";
    }
    std::cout << std::endl;

    // Print each row
    for (int row = 0; row <= max_row; ++row) {
        std::cout << row << "\t";
        for (int col = 0; col <= max_col; ++col) {
            auto col_it = cells.find(col);
            if (col_it != cells.end()) {
                auto row_it = col_it->second.find(row);
                if (row_it != col_it->second.end()) {
                    auto cell = row_it->second;
                    if (std::holds_alternative<int>(cell)) {
                        std::cout << std::get<int>(cell) << "\t";
                    }
                    else if (is_empty(cell)) {
                        std::cout << "\t";
                    }
                    else if (is_error(cell)) {
                        std::cout << "#ERR" << "\t";
                    }
                }
                else {
                    std::cout << "\t";
                }
            }
        }
        std::cout << std::endl;
    }

    // print_dependencies();
}

void Spreadsheet::parse_tokens(std::pair<int, int> cell_coords,
                               const std::string& cell_contents)
{
    // If contains dependency, record dependencies and computation for later
    if (contains_letter(cell_contents)) {
        std::string cell_address = coords_to_address(cell_coords);

        // Check if cell exists in dependencies already. Update cell_contents
        auto dep_it = dependencies.find(cell_address);
        if (dep_it != dependencies.end()) {
            dep_it->second.first = cell_contents;
        }
        else {
            dependencies[cell_address] = {cell_contents, {}};
        }

        // Update downstream dependencies
        std::istringstream iss(cell_contents);
        std::string token;
        while (iss >> token) {
            if (is_letter_number_format(token)) {
                dependencies[token].second.push_back(cell_address);
            }
        }
    }

    // Calculate prefix value
    else {
        calculate_postfix(cell_coords, cell_contents);
    }
}

void Spreadsheet::calculate_postfix(std::pair<int, int> cell_coords,
                                    const std::string& expression)
{
    std::istringstream iss(expression);
    std::string token;
    std::stack<int> operands;
    bool error = false;
    while (iss >> token) {
        if (token == "+" || token == "-" || token == "*" || token == "/") {
            if (operands.size() < 2) {
                error = true;
                break;
            }

            // Pop operands
            int op1 = operands.top();
            operands.pop();
            int op2 = operands.top();
            operands.pop();

            // Perform operation
            int result;
            if (token == "+") {
                result = op1 + op2;
            }
            else if (token == "-") {
                result = op1 - op2;
            }
            else if (token == "*") {
                result = op1 * op2;
            }
            else if (token == "/") {
                if (op2 == 0) {
                    error = true;
                    break;
                }
                result = op1 / op2;
            }
            operands.push(result);
        }
        else {
            try {
                int number = std::stoi(token);
                operands.push(number);
            }
            catch (...) {
                error = true;
                break;
            }
        }
    }
    cells[cell_coords.first][cell_coords.second] =
        (operands.size() != 1 || error) ? CellState::Error
                                        : CellValue(operands.top());
}

void Spreadsheet::resolve_dependencies()
{
    std::vector<std::string> sorted_dependencies =
        topological_sort_dependencies();

    for (const auto& cell_address : sorted_dependencies) {
        resolve_references(cell_address);
        if (!dependencies[cell_address].first.empty()) {
            auto coords = address_to_coords(cell_address);
            calculate_postfix(coords, dependencies[cell_address].first);
        }
    }
}

void Spreadsheet::resolve_references(const std::string& cell_address)
{
    const auto& formula = dependencies[cell_address].first;
    std::string resolved_formula;
    std::istringstream iss(formula);
    std::string token;
    while (iss >> token) {
        if (is_letter_number_format(token)) {
            auto coords = address_to_coords(token);
            if (cells[coords.first].contains(coords.second)) {
                if (std::holds_alternative<int>(
                        cells[coords.first][coords.second])) {
                    resolved_formula = resolved_formula + " " +
                                       std::to_string(std::get<int>(
                                           cells[coords.first][coords.second]));
                }
                else {  // Cell not an int, handle as error
                    resolved_formula = "#ERR";
                    break;
                }
            }
            else {  // Cell not defined, handle as error
                resolved_formula = "#ERR";
                break;
            }
        }
        else {
            resolved_formula = resolved_formula + " " + token;
        }
    }
    dependencies[cell_address].first = resolved_formula;
}

// Sorts topologically, but also sets error values when cycles are detected
std::vector<std::string> Spreadsheet::topological_sort_dependencies()
{
    std::unordered_set<std::string> marked;
    std::unordered_set<std::string> recStack;
    std::vector<std::string> res;
    for (const auto& [key, val] : dependencies) {
        // Contains cycle: mark all cells on path as CellState::Error
        if (!marked.contains(key) &&
            topological_dfs_helper(key, marked, recStack, res)) {
            for (const auto& cell : res) {
                auto coords = address_to_coords(cell);
                cells[coords.first][coords.second] = CellState::Error;
            }
        }
    }
    return res;
}

// Returns true if cycle detected
bool Spreadsheet::topological_dfs_helper(
    const std::string& x, std::unordered_set<std::string>& marked,
    std::unordered_set<std::string>& recStack, std::vector<std::string>& res)
{
    marked.insert(x);
    recStack.insert(x);
    bool has_cycle = false;
    for (const auto& w : dependencies[x].second) {
        if (recStack.contains(w)) {
            res.insert(res.begin(),
                       x);  // insert into res to later mark as #ERR
            return true;
        }
        else if (!marked.contains(w)) {
            has_cycle |= topological_dfs_helper(w, marked, recStack, res);
        }
    }
    res.insert(res.begin(), x);
    recStack.erase(x);
    return has_cycle;
}

std::string Spreadsheet::coord_to_col(int n)
{
    std::string result;
    while (n >= 0) {
        char letter = 'A' + (n % 26);
        result = letter + result;
        n /= 26;
        --n;
    }
    return result;
}

int Spreadsheet::col_to_coord(const std::string& col)
{
    int result = 0;
    for (char ch : col) {
        result = result * 26 + (ch - 'A');
    }
    return result;
}

std::string Spreadsheet::coords_to_address(const std::pair<int, int>& coords)
{
    return coord_to_col(coords.first) + std::to_string(coords.second);
}

std::pair<int, int> Spreadsheet::address_to_coords(const std::string& address)
{
    int idx = 0;
    while (idx < address.size() && std::isalpha(address[idx])) ++idx;
    std::string col = address.substr(0, idx);
    int row = std::stoi(address.substr(idx));
    return {col_to_coord(col), row};
}

bool Spreadsheet::contains_letter(const std::string& str)
{
    for (char ch : str) {
        if (std::isalpha(ch)) {
            return true;
        }
    }
    return false;
}

bool Spreadsheet::is_letter_number_format(const std::string& cell)
{
    std::regex pattern("^[A-Za-z]+[0-9]+$");
    return std::regex_match(cell, pattern);
}

bool Spreadsheet::is_empty(const CellValue& cell)
{
    return std::holds_alternative<CellState>(cell) &&
           std::get<CellState>(cell) == CellState::Empty;
}
bool Spreadsheet::is_error(const CellValue& cell)
{
    return std::holds_alternative<CellState>(cell) &&
           std::get<CellState>(cell) == CellState::Error;
}

void Spreadsheet::print_dependencies()
{
    for (const auto& [key, val] : dependencies) {
        std::cout << key << " formula: " << val.first << std::endl;
        std::cout << "Downstream dependencies -> ";
        for (const auto& dep : val.second) {
            std::cout << dep << ", ";
        }
        std::cout << "\b\b\n";
    }
}

int main()
{
    Spreadsheet s;
    std::cout << "TEST 1: ---------------------------\n";
    s.parse_input("input.csv");
    s.print_output();
    std::cout << "TEST 2: ---------------------------\n";
    s.clear();
    s.parse_input("input2.csv");
    s.print_output();
    std::cout << "TEST 3: ---------------------------\n";
    s.clear();
    s.parse_input("input3.csv");
    s.print_output();
    std::cout << "TEST 4: ---------------------------\n";
    s.clear();
    s.parse_input("input4.csv");
    s.print_output();
}
