// SPDX-License-Identifier: Apache-2.0

/**
 * @file heapline-report.cpp
 * @brief Merge the per-process records written by heapline into a summary of
 * the job: peak heap per rank, per node and imbalance.
 */

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef HEAPLINE_VERSION
#define HEAPLINE_VERSION "unknown"
#endif

namespace {

namespace fs = std::filesystem;

/**
 * @brief Column names of a version 1 record, in order.
 */
const char* const kColumns =
    "rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,"
    "peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,"
    "vmrss_at_exit_bytes";

/**
 * @brief One process of the job, as read from its record.
 */
struct Process {
  long rank = -1;
  std::string host;
  long pid = 0;
  std::string job;
  double elapsed_s                  = 0;
  unsigned long long allocs         = 0;
  unsigned long long frees          = 0;
  long long peak_heap_bytes         = 0;
  double peak_heap_time_s           = 0;
  long long live_heap_at_exit_bytes = 0;
  long long vmhwm_bytes             = -1;
  long long vmrss_at_exit_bytes     = -1;
  std::string values;  ///< Raw CSV values, kept for --csv.
};

/**
 * @brief Command-line options.
 */
struct Options {
  std::vector<std::string> inputs;
  std::string csv;
  size_t top = 10;
  bool all   = false;
};

void warn(const std::string& message) {
  std::fprintf(stderr, "heapline-report: %s\n", message.c_str());
}

/**
 * @brief Read one line from @p in into @p line, without a trailing CR.
 */
bool read_line(std::istream& in, std::string& line) {
  if (!std::getline(in, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return true;
}

/**
 * @brief Split @p line at commas, keeping empty fields.
 */
std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream in(line);
  while (std::getline(in, field, ',')) fields.push_back(field);
  if (!line.empty() && line.back() == ',') fields.emplace_back();
  return fields;
}

/**
 * @brief Parse all of @p text as a number; false on any leftover character.
 */
bool parse(const std::string& text, long long& value) {
  if (text.empty()) return false;
  char* end = nullptr;
  errno     = 0;
  value     = std::strtoll(text.c_str(), &end, 10);
  return errno == 0 && *end == '\0';
}

bool parse(const std::string& text, long& value) {
  long long wide = 0;
  if (!parse(text, wide)) return false;
  value = static_cast<long>(wide);
  return true;
}

bool parse(const std::string& text, unsigned long long& value) {
  if (text.empty() || text[0] == '-') return false;
  char* end = nullptr;
  errno     = 0;
  value     = std::strtoull(text.c_str(), &end, 10);
  return errno == 0 && *end == '\0';
}

bool parse(const std::string& text, double& value) {
  if (text.empty()) return false;
  char* end = nullptr;
  errno     = 0;
  value     = std::strtod(text.c_str(), &end);
  return errno == 0 && *end == '\0';
}

/**
 * @brief Read the record in @p path into @p process.
 *
 * @return An empty string on success, the reason of the failure otherwise.
 */
std::string read_record(const fs::path& path, Process& process) {
  std::ifstream in(path);
  if (!in) return "cannot be read";
  std::string tag;
  std::string columns;
  std::string values;
  if (!read_line(in, tag) || tag.rfind("# heapline ", 0) != 0)
    return "not a heapline record";
  if (tag != "# heapline 1")
    return "unsupported format version " + tag.substr(11);
  if (!read_line(in, columns) || columns != kColumns)
    return "unexpected columns";
  if (!read_line(in, values) || values.empty()) return "missing values";
  std::vector<std::string> f = split(values);
  if (f.size() != 12)
    return "expected 12 values, found " + std::to_string(f.size());

  static const char* const names[] = {"rank",
                                      "host",
                                      "pid",
                                      "job",
                                      "elapsed_s",
                                      "allocs",
                                      "frees",
                                      "peak_heap_bytes",
                                      "peak_heap_time_s",
                                      "live_heap_at_exit_bytes",
                                      "vmhwm_bytes",
                                      "vmrss_at_exit_bytes"};
  process.host                     = f[1];
  process.job                      = f[3];
  process.values                   = values;
  const bool ok[]                  = {parse(f[0], process.rank),
                                      true,
                                      parse(f[2], process.pid),
                                      true,
                                      parse(f[4], process.elapsed_s),
                                      parse(f[5], process.allocs),
                                      parse(f[6], process.frees),
                                      parse(f[7], process.peak_heap_bytes),
                                      parse(f[8], process.peak_heap_time_s),
                                      parse(f[9], process.live_heap_at_exit_bytes),
                                      parse(f[10], process.vmhwm_bytes),
                                      parse(f[11], process.vmrss_at_exit_bytes)};
  for (size_t i = 0; i < std::size(ok); ++i)
    if (!ok[i]) return std::string("bad value in column ") + names[i];
  return "";
}

/**
 * @brief Expand the directories of @p inputs into the .csv files they
 * contain, sorted by name. Other inputs are taken as files.
 */
std::vector<fs::path> collect(const std::vector<std::string>& inputs) {
  std::vector<fs::path> files;
  for (const auto& input : inputs) {
    std::error_code ec;
    if (!fs::is_directory(input, ec)) {
      files.emplace_back(input);
      continue;
    }
    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(input, ec))
      if (entry.path().extension() == ".csv") found.push_back(entry.path());
    std::sort(found.begin(), found.end());
    files.insert(files.end(), found.begin(), found.end());
  }
  return files;
}

/**
 * @brief Format a byte count with a binary unit suffix, as the library does.
 */
std::string format_bytes(long long bytes) {
  static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  auto value                       = static_cast<double>(bytes);
  size_t unit                      = 0;
  while ((value >= 1024.0 || value <= -1024.0) && unit + 1 < std::size(units)) {
    value /= 1024.0;
    ++unit;
  }
  char buf[32];
  if (unit == 0)
    std::snprintf(buf, sizeof(buf), "%lld B", bytes);
  else
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
  return buf;
}

/**
 * @brief Ranks between 0 and the highest rank seen that have no record.
 */
std::vector<long> missing_ranks(const std::vector<Process>& processes) {
  std::set<long> seen;
  for (const auto& p : processes)
    if (p.rank >= 0) seen.insert(p.rank);
  std::vector<long> missing;
  if (seen.empty()) return missing;
  for (long r = 0; r < *seen.rbegin(); ++r)
    if (!seen.count(r)) missing.push_back(r);
  return missing;
}

/**
 * @brief Comma-separated list of the first 20 values, then an ellipsis.
 */
std::string join(const std::vector<long>& values) {
  std::string out;
  for (size_t i = 0; i < values.size() && i < 20; ++i)
    out += (i ? ", " : "") + std::to_string(values[i]);
  if (values.size() > 20) out += ", ...";
  return out;
}

/**
 * @brief Print the job summary, the per-node sums and the ranks with the
 * highest heap peak to stdout.
 */
void print_report(std::vector<Process> processes, const Options& options) {
  std::sort(processes.begin(), processes.end(),
            [](const Process& a, const Process& b) {
              if (a.peak_heap_bytes != b.peak_heap_bytes)
                return a.peak_heap_bytes > b.peak_heap_bytes;
              return a.rank < b.rank;
            });

  std::map<std::string, std::pair<size_t, long long>> nodes;
  size_t name_width   = 1;
  double longest      = 0;
  long long total     = 0;
  long long max_vmhwm = -1;
  std::vector<long long> peaks;
  for (const auto& p : processes) {
    auto& node = nodes[p.host];
    ++node.first;
    if (p.vmhwm_bytes > 0) node.second += p.vmhwm_bytes;
    name_width = std::max(name_width, p.host.size());
    longest    = std::max(longest, p.elapsed_s);
    total += p.peak_heap_bytes;
    max_vmhwm = std::max(max_vmhwm, p.vmhwm_bytes);
    peaks.push_back(p.peak_heap_bytes);
  }
  std::sort(peaks.begin(), peaks.end());
  const size_t n = peaks.size();
  const long long median =
      n % 2 ? peaks[n / 2] : (peaks[n / 2 - 1] + peaks[n / 2]) / 2;
  const double mean  = static_cast<double>(total) / n;
  const Process& top = processes.front();
  const int names    = static_cast<int>(name_width);
  const int hosts    = static_cast<int>(std::max<size_t>(name_width, 4));

  std::printf("heapline-report: %zu %s on %zu %s, longest process %.1f s\n\n",
              n, n == 1 ? "rank" : "ranks", nodes.size(),
              nodes.size() == 1 ? "node" : "nodes", longest);
  std::printf("Peak heap\n");
  std::printf("  %-10s %s, rank %ld at %.1f s\n", "max",
              format_bytes(top.peak_heap_bytes).c_str(), top.rank,
              top.peak_heap_time_s);
  std::printf("  %-10s %s\n", "median", format_bytes(median).c_str());
  std::printf("  %-10s %s\n", "min", format_bytes(peaks.front()).c_str());
  std::printf("  %-10s %.2f (max / mean)\n\n", "imbalance",
              mean > 0 ? top.peak_heap_bytes / mean : 0.0);

  std::printf(
      "Per node, sum of VmHWM (an upper bound to compare with --mem)\n");
  for (const auto& [host, node] : nodes)
    std::printf("  %-*s  %zu %s  %s\n", names, host.c_str(), node.first,
                node.first == 1 ? "rank " : "ranks",
                format_bytes(node.second).c_str());

  const size_t shown = options.all ? n : std::min(options.top, n);
  std::printf("\nRanks by peak heap (%zu of %zu)\n", shown, n);
  std::printf("  %4s  %-*s  %10s  %6s  %10s  %s\n", "rank", hosts, "host",
              "peak heap", "at (s)", "VmHWM", "heap / VmHWM");
  for (size_t i = 0; i < shown; ++i) {
    const Process& p = processes[i];
    char ratio[16]   = "-";
    if (p.vmhwm_bytes > 0)
      std::snprintf(ratio, sizeof(ratio), "%.2f",
                    static_cast<double>(p.peak_heap_bytes) / p.vmhwm_bytes);
    std::printf(
        "  %4ld  %-*s  %10s  %6.1f  %10s  %s\n", p.rank, hosts, p.host.c_str(),
        format_bytes(p.peak_heap_bytes).c_str(), p.peak_heap_time_s,
        p.vmhwm_bytes >= 0 ? format_bytes(p.vmhwm_bytes).c_str() : "-", ratio);
  }

  std::printf(
      "\nSlurm: sacct MaxRSS is a per-task maximum sampled during the job. "
      "Compare it\nwith the largest VmHWM above, %s; a short peak can fall "
      "between samples.\n",
      max_vmhwm >= 0 ? format_bytes(max_vmhwm).c_str() : "unknown");
}

/**
 * @brief Write the records of @p processes, sorted by rank, as one CSV file.
 */
bool write_csv(const std::string& path, std::vector<Process> processes) {
  std::sort(processes.begin(), processes.end(),
            [](const Process& a, const Process& b) {
              if (a.rank != b.rank) return a.rank < b.rank;
              if (a.host != b.host) return a.host < b.host;
              return a.pid < b.pid;
            });
  std::ofstream out(path);
  if (!out) return false;
  out << "# heapline 1\n" << kColumns << '\n';
  for (const auto& p : processes) out << p.values << '\n';
  return static_cast<bool>(out);
}

void usage(std::FILE* out) {
  std::fprintf(out,
               "usage: heapline-report [--top N | --all] [--csv FILE] "
               "RECORD_OR_DIR...\n"
               "Summarize the records written by heapline through "
               "HEAPLINE_OUTPUT.\n"
               "  --top N     list the N ranks with the highest heap peak "
               "(default 10)\n"
               "  --all       list every rank\n"
               "  --csv FILE  also write all records, sorted by rank, to "
               "FILE\n"
               "  --version   print the version and exit\n");
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      std::printf("heapline-report %s\n", HEAPLINE_VERSION);
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      usage(stdout);
      return 0;
    }
    if (arg == "--all") {
      options.all = true;
    } else if (arg == "--csv" && i + 1 < argc) {
      options.csv = argv[++i];
    } else if (arg == "--top" && i + 1 < argc) {
      long long top = 0;
      if (!parse(argv[++i], top) || top <= 0) {
        warn("--top expects a positive number");
        return 1;
      }
      options.top = static_cast<size_t>(top);
    } else if (!arg.empty() && arg[0] == '-') {
      usage(stderr);
      return 1;
    } else {
      options.inputs.push_back(arg);
    }
  }
  if (options.inputs.empty()) {
    usage(stderr);
    return 1;
  }

  std::vector<Process> processes;
  for (const auto& file : collect(options.inputs)) {
    Process process;
    std::string error = read_record(file, process);
    if (error.empty())
      processes.push_back(std::move(process));
    else
      warn(file.string() + ": " + error);
  }
  if (processes.empty()) {
    warn("no valid record");
    return 1;
  }
  std::vector<long> missing = missing_ranks(processes);
  if (!missing.empty())
    warn("missing ranks: " + join(missing) + " (killed or never started?)");
  print_report(processes, options);
  if (!options.csv.empty() && !write_csv(options.csv, processes)) {
    warn("cannot write " + options.csv);
    return 1;
  }
  return 0;
}
