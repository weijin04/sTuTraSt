#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include "types.h"
#include <string>

struct CliOptions {
    bool show_help = false;
    bool checkpoint_every_set = false;
    bool max_kmc_steps_set = false;
    int checkpoint_every = 0;
    int max_kmc_steps = 0;
    std::string checkpoint_dir = ".";
    std::string resume_path;

    bool has_resume() const { return !resume_path.empty(); }
    bool has_kmc_control() const {
        return has_resume() || checkpoint_every_set || max_kmc_steps_set;
    }
};

CliOptions parse_cli_options(int argc, char** argv);
void validate_cli_options(const CliOptions& options, const InputParams& params);
std::string cli_help_text();

#endif // CLI_OPTIONS_H
