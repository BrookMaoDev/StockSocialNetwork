#pragma once

#include <string>
#include <vector>

// Prints `values` (oldest first) as an ASCII line chart, with the first and
// last x-axis labels (e.g. dates) under the chart.
void plot(const std::vector<double> &values, const std::string &first_label,
          const std::string &last_label);
