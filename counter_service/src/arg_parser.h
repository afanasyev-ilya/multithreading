#include <functional>
#include <sstream>
#include <stdexcept>
#include <cctype>

struct Settings {
    int num_requests {100000000};
    int max_posts {10000000};
    int reads_per_write {50};
    int max_threads {1};
    int num_shards {128};
};

class ArgParser {
    struct Option {
        std::vector<std::string> names;   // primary + aliases
        bool takes_value;
        std::string value_name;
        std::string help;
        std::function<void(const std::string&)> on_value; // for options
        std::function<void()> on_flag;                    // for flags
    };

    std::string prog_;
    std::vector<Option> options_;
    std::unordered_map<std::string, size_t> index_; // name -> option idx

    static bool starts_with(const std::string& s, const char* p) {
        return s.rfind(p, 0) == 0;
    }

    static std::string strip_dashes(const std::string& s) {
        size_t i = 0; while (i < s.size() && s[i] == '-') ++i;
        return s.substr(i);
    }

public:
    explicit ArgParser(std::string prog) : prog_(std::move(prog)) {}

    // Add an option with value (aliases allowed)
    void add_option(std::vector<std::string> names, std::string value_name,
                    std::string help, std::function<void(const std::string&)> cb) {
        Option opt;
        opt.names = std::move(names);
        opt.takes_value = true;
        opt.value_name = std::move(value_name);
        opt.help = std::move(help);
        opt.on_value = std::move(cb);
        size_t idx = options_.size();
        options_.push_back(std::move(opt));
        for (auto& n : options_.back().names) index_[n] = idx;
    }

    // Add a flag (no value)
    void add_flag(std::vector<std::string> names, std::string help, std::function<void()> cb) {
        Option opt;
        opt.names = std::move(names);
        opt.takes_value = false;
        opt.value_name = "";
        opt.help = std::move(help);
        opt.on_flag = std::move(cb);
        size_t idx = options_.size();
        options_.push_back(std::move(opt));
        for (auto& n : options_.back().names) index_[n] = idx;
    }

    // Parses argv; throws std::runtime_error on errors or unknown arguments.
    void parse(int argc, char* argv[]) const {
        for (int i = 1; i < argc; ++i) {
            std::string tok = argv[i];
            if (tok == "--") break; // end of options

            if (starts_with(tok, "--")) {
                // Long form: --key or --key=value
                auto eq = tok.find('=');
                std::string name = (eq == std::string::npos) ? tok : tok.substr(0, eq);
                auto it = index_.find(name);
                if (it == index_.end())
                    throw std::runtime_error("Unknown argument: " + name);

                const Option& opt = options_[it->second];
                if (opt.takes_value) {
                    std::string val;
                    if (eq != std::string::npos) {
                        val = tok.substr(eq + 1);
                        if (val.empty()) throw std::runtime_error("Missing value for " + name);
                    } else {
                        if (++i >= argc) throw std::runtime_error("Missing value for " + name);
                        val = argv[i];
                    }
                    opt.on_value(val);
                } else {
                    if (eq != std::string::npos)
                        throw std::runtime_error("Flag " + name + " does not take a value");
                    opt.on_flag ? opt.on_flag() : void();
                }
            } else if (starts_with(tok, "-")) {
                // Short/alias form: -t 8  (no -abc bundling)
                auto it = index_.find(tok);
                if (it == index_.end())
                    throw std::runtime_error("Unknown argument: " + tok);

                const Option& opt = options_[it->second];
                if (opt.takes_value) {
                    if (++i >= argc) throw std::runtime_error("Missing value for " + tok);
                    opt.on_value(argv[i]);
                } else {
                    opt.on_flag ? opt.on_flag() : void();
                }
            } else {
                // Positional not supported in this app -> error
                throw std::runtime_error("Unknown positional: " + tok);
            }
        }
    }

    std::string usage() const {
        std::ostringstream os;
        os << "Usage: " << prog_ << " [options]\n\nOptions:\n";
        size_t pad = 0;
        std::vector<std::string> lines;
        lines.reserve(options_.size());

        auto join_names = [](const std::vector<std::string>& v) {
            std::ostringstream s;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) s << ", ";
                s << v[i];
            }
            return s.str();
        };

        for (auto& o : options_) {
            std::string names = join_names(o.names);
            if (o.takes_value) names += " " + o.value_name;
            pad = std::max(pad, names.size());
            lines.push_back(std::move(names));
        }

        for (size_t i = 0; i < options_.size(); ++i) {
            os << "  " << lines[i];
            if (lines[i].size() < pad) os << std::string(pad - lines[i].size(), ' ');
            os << "  " << options_[i].help << "\n";
        }
        return os.str();
    }
};

// ---- small helpers for typed parsing & validation
inline int to_int(const std::string& s, const char* flag) {
    try {
        size_t pos = 0;
        long v = std::stol(s, &pos, 10);
        if (pos != s.size()) throw std::runtime_error("");
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
            throw std::runtime_error("");
        return static_cast<int>(v);
    } catch (...) {
        throw std::runtime_error(std::string("Invalid integer for ") + flag + ": " + s);
    }
}


void load_cli_settings(Settings &settings, int argc, char* argv[]) {
    ArgParser p(argv[0]);

    // --help
    bool asked_help = false;
    p.add_flag({"-h", "--help"}, "Show this help message", [&]() {
        asked_help = true;
        std::cout << p.usage();
        std::exit(0);
    });

    // --num-requests
    p.add_option({"--num-requests"}, "INT",
                 "Total number of requests (default: " + std::to_string(settings.num_requests) + ")",
                 [&](const std::string& v) {
                     settings.num_requests = to_int(v, "--num-requests");
                 });

    // -t / --threads / --num-threads
    p.add_option({"-t", "--threads", "--max-threads"}, "INT",
                 "Number of worker threads (default: " + std::to_string(settings.max_threads) + ")",
                 [&](const std::string& v) {
                     settings.max_threads = to_int(v, "--threads");
                 });

    // --posts
    p.add_option({"--posts"}, "INT",
                 "Max distinct post IDs (default: " + std::to_string(settings.max_posts) + ")",
                 [&](const std::string& v) {
                     settings.max_posts = to_int(v, "--posts");
                 });

    // --reads-per-write
    p.add_option({"--reads-per-write"}, "INT",
                 "Read ops per write op (>=1) (default: " + std::to_string(settings.reads_per_write) + ")",
                 [&](const std::string& v) {
                     settings.reads_per_write = to_int(v, "--reads-per-write");
                     if (settings.reads_per_write < 1)
                         throw std::runtime_error("--reads-per-write must be >= 1");
                 });

    // --reads-per-write
    p.add_option({"--shards"}, "INT",
                 "Number of shards (>=1) (default: " + std::to_string(settings.num_shards) + ")",
                 [&](const std::string& v) {
                     settings.num_shards = to_int(v, "--shards");
                     if (settings.reads_per_write < 1)
                         throw std::runtime_error("--shards >= 1");
                 });

    try {
        p.parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n\n" << p.usage();
        std::exit(1);
    }
}