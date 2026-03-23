#ifndef PRODUCT_MANAGER_H
#define PRODUCT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

/**
 * @class ProductManager
 * @brief Restores V1 "Industrial" dynamic peripheral configuration
 * 
 * Manages JSON-defined hardware profiles (Products) stored in LittleFS.
 * Allows the 5-device fleet (V2/V3/V4) to reconfigure GPIOs, schedules,
 * and alerts without firmware recompilation.
 */
class ProductManager {
public:
    static ProductManager& getInstance() {
        static ProductManager instance;
        return instance;
    }

    // Lifecycle
    bool init();
    void restoreActiveProduct();

    // Product Management
    bool saveProduct(const String& name, const String& json);
    bool loadProduct(const String& name);
    void broadcastRegistry();
    String listProducts();
    String getActiveProduct() const { return _activeProduct; }

private:
    ProductManager() = default;
    String _activeProduct;

    void _ensureDir();
    String _productPath(const String& name);

    // Apply JSON components
    void _applyPins(const JsonArray& pins);
    void _applySchedules(const JsonArray& schedules);
    void _applyAlerts(const JsonArray& alerts);
};

#endif // PRODUCT_MANAGER_H
