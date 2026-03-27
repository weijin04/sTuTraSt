#include "cli_options.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace {

int parse_positive_int(const std::string& value, const std::string& flag_name) {
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        throw std::runtime_error("Invalid integer for " + flag_name + ": " + value);
    }
    if (parsed <= 0) {
        throw std::runtime_error(flag_name + " must be > 0");
    }
    return static_cast<int>(parsed);
}

std::string require_value(int argc, char** argv, int& i, const std::string& flag_name) {
    if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + flag_name);
    }
    i++;
    return argv[i];
}

} // namespace

CliOptions parse_cli_options(int argc, char** argv) {
    CliOptions options;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.show_help = true;
            continue;
        }
        if (arg == "--resume") {
            options.resume_path = require_value(argc, argv, i, arg);
            continue;
        }
        if (arg == "--checkpoint-every") {
            options.checkpoint_every = parse_positive_int(require_value(argc, argv, i, arg), arg);
            options.checkpoint_every_set = true;
            continue;
        }
        if (arg == "--checkpoint-dir") {
            options.checkpoint_dir = require_value(argc, argv, i, arg);
            continue;
        }
        if (arg == "--max-kmc-steps") {
            options.max_kmc_steps = parse_positive_int(require_value(argc, argv, i, arg), arg);
            options.max_kmc_steps_set = true;
            continue;
        }

        throw std::runtime_error("Unknown option: " + arg);
    }

    return options;
}

void validate_cli_options(const CliOptions& options, const InputParams& params) {
    if (!options.has_kmc_control()) {
        return;
    }

    if (!params.run_kmc) {
        throw std::runtime_error("Checkpoint/resume options require run_kmc=1 in input.param");
    }
    if (params.n_temps != 1) {
        throw std::runtime_error("Phase 1 checkpoint/resume supports exactly one temperature");
    }
    if (params.n_runs != 1) {
        throw std::runtime_error("Phase 1 checkpoint/resume supports exactly one kMC run");
    }
    if (params.n_steps <= 0) {
        throw std::runtime_error("Checkpoint/resume requires n_steps > 0");
    }
}

std::string cli_help_text() {
    std::ostringstream oss;
    oss << "Usage: tutrast [options]\n\n"
        << "Options:\n"
        << "  --help, -h               Show this help text\n"
        << "  --resume <checkpoint>    Resume a single-run kMC checkpoint\n"
        << "  --checkpoint-every <N>   Write a full checkpoint every N kMC steps\n"
        << "  --checkpoint-dir <dir>   Directory for fresh-run checkpoint files (default: current dir)\n"
        << "  --max-kmc-steps <N>      Advance at most N kMC steps in this invocation\n";
    return oss.str();
}
