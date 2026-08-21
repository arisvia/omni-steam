#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Manager {

enum class DiagnosticLevel { Pass, Warning, Error, Info };

struct DiagnosticItem {
    std::string category;
    std::string name;
    DiagnosticLevel level = DiagnosticLevel::Pass;
    std::string message;
    std::string recommendation;
};

struct DoctorReport {
    bool overallHealthy = true;
    int passCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    std::vector<DiagnosticItem> items;
};

class Doctor {
public:
    static DoctorReport RunDiagnostics();
    static void PrintReport(const DoctorReport& report);
};

} // namespace Manager
