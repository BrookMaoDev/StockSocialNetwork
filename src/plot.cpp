#include "plot.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

static const int MAX_WIDTH = 70;
static const int HEIGHT = 15;
static const int LABEL_WIDTH = 10;

void plot(const std::vector<double> &values, const std::string &first_label,
          const std::string &last_label) {
    if (values.empty()) {
        return;
    }
    int n = values.size();
    int width = std::min(n, MAX_WIDTH);
    auto [lo_it, hi_it] = std::minmax_element(values.begin(), values.end());
    double lo = *lo_it, hi = *hi_it;
    if (hi == lo) { // flat line: avoid dividing by zero
        hi += 1;
        lo -= 1;
    }

    // grid[0] is the top row. Each column gets one '*'.
    std::vector<std::string> grid(HEIGHT, std::string(width, ' '));

    // Column c covers values [c*n/width, (c+1)*n/width); plot the last one,
    // i.e. the close at the end of that stretch of days.
    for (int c = 0; c < width; ++c) {
        int interval_end = (c + 1) * n / width;
        double price = values[interval_end - 1];
        double row_from_bottom = (price - lo) / (hi - lo) * (HEIGHT - 1);
        int row = std::lround(HEIGHT - 1 - row_from_bottom);
        grid[row][c] = '*';
    }

    std::cout << std::fixed << std::setprecision(2);
    for (int row = 0; row < HEIGHT; ++row) {
        std::cout << std::setw(LABEL_WIDTH);
        if (row == 0) {
            std::cout << hi;
        } else if (row == HEIGHT - 1) {
            std::cout << lo;
        } else {
            std::cout << "";
        }
        std::cout << " |" << grid[row] << "\n";
    }
    std::cout << std::string(LABEL_WIDTH + 1, ' ') << '+' << std::string(width, '-') << "\n";

    int gap = std::max(1, width - (int)first_label.size() - (int)last_label.size());
    std::cout << std::string(LABEL_WIDTH + 2, ' ') << first_label << std::string(gap, ' ')
              << last_label << std::endl;
    std::cout << std::defaultfloat;
}
