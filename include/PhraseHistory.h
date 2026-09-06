#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace whisperflow {

// A bounded, local-only log of successfully transcribed phrases. There is no
// cloud sync and no file beyond the one next to settings.json. It backs the
// tray's "Repeat last insertion".
class PhraseHistory {
public:
    static constexpr std::size_t kDefaultMaxEntries = 200;

    explicit PhraseHistory(std::size_t maxEntries = kDefaultMaxEntries);

    // Replaces the current content from the JSON file. Missing/corrupt files are
    // not fatal: the history just starts empty.
    bool load(const std::filesystem::path& path);

    // Writes the JSON array and creates parent directories as needed.
    bool save(std::string& error) const;

    // Adds a phrase. A phrase identical to the current last one is not stored
    // twice; the oldest entries are dropped when the limit is exceeded.
    void add(const std::string& phrase);

    // Non-owning accessors.
    [[nodiscard]] const std::string& last() const;
    [[nodiscard]] const std::string& at(std::size_t index) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t maxEntries() const noexcept;
    void setMaxEntries(std::size_t maxEntries);
    void clear();

    // Pure helpers exposed for tests. parse returns false on a corrupt array;
    // entries already present in the document survive malformed trailing parts
    // where possible.
    [[nodiscard]] static bool parseJson(const std::string& json,
                                        std::vector<std::string>& out);
    [[nodiscard]] static std::string toJson(const std::vector<std::string>& entries);

private:
    std::filesystem::path path_;
    std::vector<std::string> entries_;
    std::size_t maxEntries_{kDefaultMaxEntries};
};

}  // namespace whisperflow
