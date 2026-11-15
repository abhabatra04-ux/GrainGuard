#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <sstream>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cctype>

using namespace std;

// ============================================================
//  CUSTOM EXCEPTIONS
// ============================================================

class AgriBinException : public runtime_error {
public:
    explicit AgriBinException(const string& message) : runtime_error(message) {}
};

class InvalidStockException : public AgriBinException {
public:
    explicit InvalidStockException(const string& message) : AgriBinException(message) {}
};

class SiloNotFoundException : public AgriBinException {
public:
    explicit SiloNotFoundException(const string& message) : AgriBinException(message) {}
};

class InputClosedException : public AgriBinException {
public:
    InputClosedException() : AgriBinException("Input stream closed. Exiting safely.") {}
};

// ============================================================
//  UTILITY NAMESPACE
// ============================================================

namespace Util {
    inline string trim(const string& value) {
        const string whitespace = " \t\n\r\f\v";
        const size_t first = value.find_first_not_of(whitespace);
        if (first == string::npos) return "";
        const size_t last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    inline bool isFinite(double value) {
        return std::isfinite(value) != 0;
    }

    inline string timestampNow() {
        const time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());
        const tm* local = std::localtime(&now);
        if (local == NULL) return "Unknown time";
        ostringstream out;
        out << put_time(local, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }
}

// ============================================================
//  ABSTRACT BASE CLASS: CropSilo
// ============================================================

class CropSilo {
private:
    int id_;
    string cropType_;
    double currentStock_;   // metric tons
    double maxCapacity_;    // metric tons
    double moistureLevel_;  // percentage

    static void validateCommon(int id, const string& crop, double stock,
                               double capacity, double moisture) {
        if (id <= 0)
            throw invalid_argument("Silo ID must be a positive integer.");
        if (Util::trim(crop).empty())
            throw invalid_argument("Crop name cannot be empty.");
        if (!Util::isFinite(capacity) || capacity <= 0.0)
            throw invalid_argument("Silo capacity must be a finite value greater than zero.");
        if (!Util::isFinite(stock) || stock < 0.0 || stock > capacity)
            throw invalid_argument("Initial stock must be finite and between zero and maximum capacity.");
        validateMoistureValue(moisture);
    }

protected:
    static void validateMoistureValue(double moisture) {
        if (!Util::isFinite(moisture) || moisture < 0.0 || moisture > 100.0)
            throw invalid_argument("Moisture percentage must be between 0 and 100.");
    }

public:
    CropSilo(int id, const string& crop, double stock, double capacity, double moisture)
        : id_(id), cropType_(Util::trim(crop)), currentStock_(stock),
          maxCapacity_(capacity), moistureLevel_(moisture) {
        validateCommon(id_, cropType_, currentStock_, maxCapacity_, moistureLevel_);
    }

    virtual ~CropSilo() = default;

    // Getters
    int    getId()            const { return id_; }
    string getCropType()      const { return cropType_; }
    double getCurrentStock()  const { return currentStock_; }
    double getMaxCapacity()   const { return maxCapacity_; }
    double getMoistureLevel() const { return moistureLevel_; }
    double getFillPercentage()const { return (currentStock_ / maxCapacity_) * 100.0; }

    // Stock operations
    void addStock(double quantity) {
        if (!Util::isFinite(quantity) || quantity <= 0.0)
            throw InvalidStockException("Quantity to add must be a finite positive number.");
        if (quantity > maxCapacity_ - currentStock_)
            throw InvalidStockException("Operation rejected: maximum silo capacity would be exceeded.");
        currentStock_ += quantity;
    }

    void removeStock(double quantity) {
        if (!Util::isFinite(quantity) || quantity <= 0.0)
            throw InvalidStockException("Quantity to remove must be a finite positive number.");
        if (quantity > currentStock_)
            throw InvalidStockException("Operation rejected: insufficient stock available in silo.");
        currentStock_ -= quantity;
    }

    void updateMoisture(double moisture) {
        validateMoistureValue(moisture);
        moistureLevel_ = moisture;
    }

    // Pure virtual interface
    virtual string         getTypeString()                  const = 0;
    virtual void           displayDetails(ostream& os)      const = 0;
    virtual vector<string> collectAlerts()                  const = 0;
    virtual void           serialize(ostream& os)           const = 0;
};

// ============================================================
//  DERIVED CLASS: StandardSilo
// ============================================================

class StandardSilo : public CropSilo {
public:
    StandardSilo(int id, const string& crop, double stock, double capacity, double moisture)
        : CropSilo(id, crop, stock, capacity, moisture) {}

    string getTypeString() const override { return "STANDARD"; }

    void displayDetails(ostream& os) const override {
        ostringstream moisture;
        moisture << fixed << setprecision(1) << getMoistureLevel() << "%";
        os << left  << setw(7)  << getId()
           << setw(12) << "Standard"
           << setw(16) << getCropType()
           << right << setw(12) << fixed << setprecision(1) << getCurrentStock()
           << setw(14) << getMaxCapacity()
           << setw(10) << moisture.str()
           << setw(14) << "N/A" << '\n';
    }

    vector<string> collectAlerts() const override {
        vector<string> alerts;
        if (getFillPercentage() < 20.0)
            alerts.push_back("Silo " + to_string(getId()) + " (" + getCropType() +
                             ") is critically low (below 20% capacity).");
        if (getMoistureLevel() > 14.0)
            alerts.push_back("Silo " + to_string(getId()) + " (" + getCropType() +
                             ") has high moisture (" + to_string(getMoistureLevel()) + "%). Spoilage warning.");
        return alerts;
    }

    void serialize(ostream& os) const override {
        os << "STANDARD " << getId() << ' ' << quoted(getCropType()) << ' '
           << setprecision(17) << getCurrentStock() << ' '
           << getMaxCapacity() << ' ' << getMoistureLevel() << '\n';
    }
};

// ============================================================
//  DERIVED CLASS: ColdStorageSilo
// ============================================================

class ColdStorageSilo : public CropSilo {
private:
    double temperature_; // Celsius

    static void validateTemperatureValue(double temperature) {
        if (!Util::isFinite(temperature) || temperature < -50.0 || temperature > 100.0)
            throw invalid_argument(
                "Temperature must be a finite value between -50 and 100 degrees Celsius.");
    }

public:
    ColdStorageSilo(int id, const string& crop, double stock, double capacity,
                    double moisture, double temperature)
        : CropSilo(id, crop, stock, capacity, moisture), temperature_(temperature) {
        validateTemperatureValue(temperature_);
    }

    double getTemperature() const { return temperature_; }

    static void validateTemperature(double temperature) {
        validateTemperatureValue(temperature);
    }

    void updateTemperature(double temperature) {
        validateTemperatureValue(temperature);
        temperature_ = temperature;
    }

    string getTypeString() const override { return "COLD"; }

    void displayDetails(ostream& os) const override {
        ostringstream temp;
        temp << fixed << setprecision(1) << temperature_ << " C";
        os << left  << setw(7)  << getId()
           << setw(12) << "Cold"
           << setw(16) << getCropType()
           << right << setw(12) << fixed << setprecision(1) << getCurrentStock()
           << setw(14) << getMaxCapacity()
           << setw(10) << fixed << setprecision(1) << getMoistureLevel()
           << setw(14) << temp.str() << '\n';
    }

    vector<string> collectAlerts() const override {
        vector<string> alerts;
        if (getFillPercentage() < 20.0)
            alerts.push_back("Silo " + to_string(getId()) + " (" + getCropType() +
                             ") is critically low (below 20% capacity).");
        if (getMoistureLevel() > 14.0)
            alerts.push_back("Silo " + to_string(getId()) + " (" + getCropType() +
                             ") has high moisture (" + to_string(getMoistureLevel()) + "%). Spoilage warning.");
        if (temperature_ > 4.0)
            alerts.push_back("Silo " + to_string(getId()) + " (" + getCropType() +
                             ") temperature exceeds the 4 C project threshold (" +
                             to_string(temperature_) + " C). Rot risk.");
        return alerts;
    }

    void serialize(ostream& os) const override {
        os << "COLD " << getId() << ' ' << quoted(getCropType()) << ' '
           << setprecision(17) << getCurrentStock() << ' '
           << getMaxCapacity() << ' ' << getMoistureLevel() << ' '
           << temperature_ << '\n';
    }
};

// ============================================================
//  SERVICE CLASS: AlertManager
// ============================================================

class AlertManager {
private:
    string logFilename_;

public:
    explicit AlertManager(const string& filename) : logFilename_(filename) {}

    bool logAlerts(const vector<string>& alerts) const {
        if (alerts.empty()) return true;
        ofstream file(logFilename_.c_str(), ios::app);
        if (!file) {
            cerr << "System Error: Could not open alert history file for writing.\n";
            return false;
        }
        const string timestamp = Util::timestampNow();
        for (const string& alert : alerts)
            file << '[' << timestamp << "] " << alert << '\n';
        file.flush();
        if (!file) {
            cerr << "System Error: Failed while writing alert history.\n";
            return false;
        }
        return true;
    }

    void showHistory() const {
        ifstream file(logFilename_.c_str());
        cout << "\n============================================================\n"
             << "                    ALERT HISTORY                          \n"
             << "============================================================\n";
        if (!file) {
            cout << "No historical alerts recorded yet.\n";
            return;
        }
        string line;
        size_t count = 0;
        while (getline(file, line)) {
            cout << line << '\n';
            ++count;
        }
        if (count == 0) cout << "Alert history is empty.\n";
        if (file.bad()) cout << "Warning: An error occurred while reading the alert history.\n";
    }
};

// ============================================================
//  CONTROLLER CLASS: GridManager
// ============================================================

class GridManager {
private:
    string regionName_;
    vector<unique_ptr<CropSilo>> siloNetwork_;
    AlertManager alertManager_;
    string dataFilename_;

    // --- Internal helpers ---

    bool idExists(int id) const {
        return any_of(siloNetwork_.begin(), siloNetwork_.end(),
            [id](const unique_ptr<CropSilo>& s) { return s && s->getId() == id; });
    }

    CropSilo* findSiloById(int id) {
        auto it = find_if(siloNetwork_.begin(), siloNetwork_.end(),
            [id](const unique_ptr<CropSilo>& s) { return s && s->getId() == id; });
        if (it == siloNetwork_.end())
            throw SiloNotFoundException("Silo with ID " + to_string(id) + " does not exist.");
        return it->get();
    }

    const CropSilo* findSiloById(int id) const {
        auto it = find_if(siloNetwork_.begin(), siloNetwork_.end(),
            [id](const unique_ptr<CropSilo>& s) { return s && s->getId() == id; });
        if (it == siloNetwork_.end())
            throw SiloNotFoundException("Silo with ID " + to_string(id) + " does not exist.");
        return it->get();
    }

    string readLine(const string& prompt) const {
        cout << prompt;
        string line;
        if (!getline(cin, line)) throw InputClosedException();
        return line;
    }

    int readInt(const string& prompt) const {
        while (true) {
            const string line = readLine(prompt);
            istringstream input(line);
            int value;
            if (input >> value) {
                input >> ws;
                if (input.eof()) return value;
            }
            cout << "Invalid input. Please enter a whole number.\n";
        }
    }

    double readDouble(const string& prompt) const {
        while (true) {
            const string line = readLine(prompt);
            istringstream input(line);
            double value;
            if (input >> value) {
                input >> ws;
                if (input.eof() && Util::isFinite(value)) return value;
            }
            cout << "Invalid input. Please enter a finite numeric value.\n";
        }
    }

    string readNonEmptyText(const string& prompt) const {
        while (true) {
            const string value = Util::trim(readLine(prompt));
            if (!value.empty()) return value;
            cout << "This field cannot be empty. Please try again.\n";
        }
    }

    void printTableHeader() const {
        cout << "\n-----------------------------------------------------------------------------------------------\n"
             << left  << setw(7)  << "ID"
             << setw(12) << "Type"
             << setw(16) << "Crop"
             << right << setw(12) << "Stock(T)"
             << setw(14) << "Capacity(T)"
             << setw(10) << "Moist(%)"
             << setw(14) << "Temp"
             << "\n-----------------------------------------------------------------------------------------------\n";
    }

public:
    explicit GridManager(const string& region)
        : regionName_(Util::trim(region)),
          alertManager_("silo_alerts.txt"),
          dataFilename_("silos.txt") {
        if (regionName_.empty())
            throw invalid_argument("Region name cannot be empty.");
    }

    GridManager(const GridManager&)            = delete;
    GridManager& operator=(const GridManager&) = delete;

    // --- Public operations ---

    void connectSilo(unique_ptr<CropSilo> silo) {
        if (!silo) throw invalid_argument("Cannot connect an empty silo pointer.");
        if (idExists(silo->getId()))
            throw invalid_argument("Silo ID already exists in this regional network.");
        siloNetwork_.push_back(move(silo));
    }

    void displayAll() const {
        printTableHeader();
        if (siloNetwork_.empty())
            cout << "No active silos are currently registered.\n";
        else
            for (const auto& silo : siloNetwork_) silo->displayDetails(cout);
        cout << "-----------------------------------------------------------------------------------------------\n";
    }

    void addSiloFromMenu() {
        cout << "\n--- Add New Storage Silo ---\n";
        const int    id       = readInt("Enter unique silo ID: ");
        if (id <= 0) throw invalid_argument("Silo ID must be positive.");
        if (idExists(id)) throw invalid_argument("A silo with this ID already exists.");
        const string crop     = readNonEmptyText("Enter crop name: ");
        const double capacity = readDouble("Enter maximum capacity (metric tons): ");
        const double stock    = readDouble("Enter initial stock (metric tons): ");
        const double moisture = readDouble("Enter moisture percentage: ");
        const int    type     = readInt("Select type (1 = Standard, 2 = Cold Storage): ");
        if (type == 1) {
            connectSilo(unique_ptr<CropSilo>(new StandardSilo(id, crop, stock, capacity, moisture)));
            cout << "Standard silo added successfully.\n";
        } else if (type == 2) {
            const double temperature = readDouble("Enter temperature in Celsius: ");
            connectSilo(unique_ptr<CropSilo>(
                new ColdStorageSilo(id, crop, stock, capacity, moisture, temperature)));
            cout << "Cold storage silo added successfully.\n";
        } else {
            cout << "Invalid silo type selection. No silo was added.\n";
        }
    }

    void updateStockMenu(bool isAdding) {
        const int    id       = readInt("Enter target silo ID: ");
        CropSilo*    silo     = findSiloById(id);
        const double quantity = readDouble("Enter quantity in metric tons: ");
        if (isAdding) {
            silo->addStock(quantity);
            cout << "Stock added successfully. New stock: "
                 << fixed << setprecision(2) << silo->getCurrentStock() << " metric tons.\n";
        } else {
            silo->removeStock(quantity);
            cout << "Stock removed successfully. New stock: "
                 << fixed << setprecision(2) << silo->getCurrentStock() << " metric tons.\n";
        }
    }

    void updateSensorMenu() {
        const int   id       = readInt("Enter target silo ID: ");
        CropSilo*   silo     = findSiloById(id);
        const double moisture = readDouble("Enter new moisture percentage: ");
        ColdStorageSilo* coldSilo = dynamic_cast<ColdStorageSilo*>(silo);
        double temperature = 0.0;
        if (coldSilo != NULL) {
            temperature = readDouble("Enter new temperature in Celsius: ");
            ColdStorageSilo::validateTemperature(temperature);
        }
        silo->updateMoisture(moisture);
        if (coldSilo != NULL) coldSilo->updateTemperature(temperature);
        cout << "Sensor readings updated successfully.\n";
    }

    void searchSilo() const {
        const int id = readInt("Enter silo ID to search: ");
        try {
            const CropSilo* silo = findSiloById(id);
            printTableHeader();
            silo->displayDetails(cout);
            cout << "-----------------------------------------------------------------------------------------------\n";
        } catch (const SiloNotFoundException& ex) {
            cout << ex.what() << '\n';
        }
    }

    void removeSilo() {
        const int id = readInt("Enter silo ID to remove: ");
        auto it = find_if(siloNetwork_.begin(), siloNetwork_.end(),
            [id](const unique_ptr<CropSilo>& s) { return s && s->getId() == id; });
        if (it == siloNetwork_.end()) {
            cout << "Silo ID " << id << " was not found.\n";
            return;
        }
        siloNetwork_.erase(it);
        cout << "Silo removed successfully.\n";
    }

    void runDiagnostics() const {
        cout << "\n============================================================\n"
             << "              GRAINGUARD NETWORK DIAGNOSTICS                \n"
             << "============================================================\n";
        vector<string> networkAlerts;
        for (const auto& silo : siloNetwork_) {
            const vector<string> alerts = silo->collectAlerts();
            networkAlerts.insert(networkAlerts.end(), alerts.begin(), alerts.end());
        }
        if (networkAlerts.empty()) {
            cout << "All registered silos are within the configured alert thresholds.\n";
        } else {
            for (const string& alert : networkAlerts)
                cout << "[ACTIVE ALERT] " << alert << '\n';
            if (alertManager_.logAlerts(networkAlerts))
                cout << "Alerts appended to silo_alerts.txt.\n";
            else
                cout << "Warning: Alerts were displayed but could not be saved to history.\n";
        }
    }

    void inventorySummary() const {
        double totalStock    = 0.0;
        double totalCapacity = 0.0;
        for (const auto& silo : siloNetwork_) {
            totalStock    += silo->getCurrentStock();
            totalCapacity += silo->getMaxCapacity();
        }
        cout << "\n============================================================\n"
             << "                REGIONAL INVENTORY SUMMARY                 \n"
             << "============================================================\n"
             << fixed << setprecision(2)
             << "Total stock managed       : " << totalStock    << " metric tons\n"
             << "Total storage capacity    : " << totalCapacity << " metric tons\n"
             << "Overall capacity used     : "
             << (totalCapacity > 0.0 ? (totalStock / totalCapacity) * 100.0 : 0.0) << "%\n"
             << "Number of registered silos: " << siloNetwork_.size() << '\n'
             << "============================================================\n";
    }

    bool saveData() const {
        const string tempFile   = dataFilename_ + ".tmp";
        const string backupFile = dataFilename_ + ".bak";
        {
            ofstream file(tempFile.c_str(), ios::out | ios::trunc);
            if (!file) {
                cerr << "Error: Unable to create temporary configuration file.\n";
                return false;
            }
            for (const auto& silo : siloNetwork_) silo->serialize(file);
            file.flush();
            if (!file) {
                cerr << "Error: Writing configuration data failed.\n";
                file.close();
                std::remove(tempFile.c_str());
                return false;
            }
        }
        std::remove(backupFile.c_str());
        ifstream existing(dataFilename_.c_str());
        const bool hadOriginal = existing.good();
        existing.close();
        if (hadOriginal && std::rename(dataFilename_.c_str(), backupFile.c_str()) != 0) {
            cerr << "Error: Could not move the previous data file to backup.\n";
            std::remove(tempFile.c_str());
            return false;
        }
        if (std::rename(tempFile.c_str(), dataFilename_.c_str()) != 0) {
            cerr << "Error: Could not install the new data file. Attempting recovery.\n";
            if (hadOriginal) std::rename(backupFile.c_str(), dataFilename_.c_str());
            std::remove(tempFile.c_str());
            return false;
        }
        if (hadOriginal) std::remove(backupFile.c_str());
        cout << "Configuration data saved successfully.\n";
        return true;
    }

    void loadData() {
        ifstream file(dataFilename_.c_str());
        if (!file) return; // First run: sample data seeded later.
        vector<unique_ptr<CropSilo>> loaded;
        string line;
        size_t lineNumber = 0;
        size_t skipped    = 0;
        while (getline(file, line)) {
            ++lineNumber;
            if (Util::trim(line).empty()) continue;
            istringstream record(line);
            string type, crop;
            int    id = 0;
            double stock = 0.0, capacity = 0.0, moisture = 0.0, temperature = 0.0;
            try {
                if (!(record >> type >> id >> quoted(crop) >> stock >> capacity >> moisture))
                    throw invalid_argument("incomplete or malformed silo record");
                unique_ptr<CropSilo> silo;
                if (type == "STANDARD") {
                    silo.reset(new StandardSilo(id, crop, stock, capacity, moisture));
                } else if (type == "COLD") {
                    if (!(record >> temperature))
                        throw invalid_argument("cold-storage record is missing temperature");
                    silo.reset(new ColdStorageSilo(id, crop, stock, capacity, moisture, temperature));
                } else {
                    throw invalid_argument("unknown silo type '" + type + "'");
                }
                record >> ws;
                if (!record.eof())
                    throw invalid_argument("unexpected extra data at end of record");
                const bool duplicate = any_of(loaded.begin(), loaded.end(),
                    [&silo](const unique_ptr<CropSilo>& e) {
                        return e->getId() == silo->getId(); });
                if (duplicate)
                    throw invalid_argument("duplicate silo ID " + to_string(silo->getId()));
                loaded.push_back(move(silo));
            } catch (const exception& ex) {
                ++skipped;
                cerr << "Skipping invalid record at line " << lineNumber
                     << ": " << ex.what() << '\n';
            }
        }
        if (file.bad()) {
            cerr << "Warning: A file read error occurred. Existing in-memory data was not replaced.\n";
            return;
        }
        siloNetwork_.swap(loaded);
        cout << "GrainGuard loaded " << siloNetwork_.size() << " valid silo record(s)";
        if (skipped > 0) cout << " and skipped " << skipped << " invalid record(s)";
        cout << ".\n";
    }

    void seedSampleData() {
        if (!siloNetwork_.empty()) return;
        connectSilo(unique_ptr<CropSilo>(new StandardSilo(101, "Wheat",    450.0, 500.0, 11.0)));
        connectSilo(unique_ptr<CropSilo>(new StandardSilo(102, "Rice",      35.0, 300.0, 12.0)));
        connectSilo(unique_ptr<CropSilo>(new ColdStorageSilo(201, "Potatoes", 180.0, 200.0, 13.0, 2.1)));
        connectSilo(unique_ptr<CropSilo>(new ColdStorageSilo(202, "Tomatoes", 120.0, 150.0, 15.5, 7.8)));
        cout << "Sample silo data initialized.\n";
    }

    void showAlertHistory() const {
        alertManager_.showHistory();
    }

    void runMenu() {
        while (true) {
            cout << "\n==========================================================\n"
                 << "            GRAINGUARD SILO MANAGEMENT SYSTEM            \n"
                 << "==========================================================\n"
                 << "Region: " << regionName_ << "\n\n"
                 << " 1.  Add new silo\n"
                 << " 2.  View all silos\n"
                 << " 3.  Add stock\n"
                 << " 4.  Remove stock\n"
                 << " 5.  Update sensor readings\n"
                 << " 6.  Search silo by ID\n"
                 << " 7.  Remove silo\n"
                 << " 8.  Run network diagnostics\n"
                 << " 9.  View inventory summary\n"
                 << "10.  View alert history\n"
                 << "11.  Save data\n"
                 << " 0.  Save and exit\n";
            int choice;
            try {
                choice = readInt("\nEnter your choice: ");
            } catch (const InputClosedException&) {
                cout << "\nInput closed. Saving before exit.\n";
                saveData();
                return;
            }
            try {
                switch (choice) {
                    case 1:  addSiloFromMenu();        break;
                    case 2:  displayAll();             break;
                    case 3:  updateStockMenu(true);    break;
                    case 4:  updateStockMenu(false);   break;
                    case 5:  updateSensorMenu();       break;
                    case 6:  searchSilo();             break;
                    case 7:  removeSilo();             break;
                    case 8:  runDiagnostics();         break;
                    case 9:  inventorySummary();       break;
                    case 10: showAlertHistory();       break;
                    case 11: saveData();               break;
                    case 0:
                        saveData();
                        cout << "Thank you for using GrainGuard.\n";
                        return;
                    default:
                        cout << "Invalid menu choice. Please select 0 to 11.\n";
                }
            } catch (const InputClosedException&) {
                cout << "\nInput closed. Saving before exit.\n";
                saveData();
                return;
            } catch (const exception& ex) {
                cout << "Operation failed: " << ex.what() << '\n';
            }
        }
    }
};

// ============================================================
//  MAIN
// ============================================================

int main() {
    try {
        GridManager punjabHub("Punjab Central Distribution Hub");
        punjabHub.loadData();
        punjabHub.seedSampleData();
        punjabHub.runMenu();
    } catch (const exception& ex) {
        cerr << "Fatal application error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
