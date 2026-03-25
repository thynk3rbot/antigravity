#pragma once
#include "../arduino_stubs.h"

class ProductManager {
public:
    static ProductManager& getInstance() { static ProductManager inst; return inst; }
    String getActiveProduct() { return "None"; }
    String listProducts() { return "[]"; }
    bool loadProduct(const String&) { return true; }
};
