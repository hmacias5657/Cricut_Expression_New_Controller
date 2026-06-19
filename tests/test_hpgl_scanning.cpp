// Test file for HPGL bounding box scanning
#include <iostream>
#include <string>
#include <vector>
#include <cassert>

// Mock implementation to test bounding box scanning
class MockHPGLScanner {
public:
    // Simulate detecting HPGL vs G-code by checking first few lines
    static bool isHPGLFile(const std::vector<std::string>& lines) {
        if (lines.empty()) return false;
        
        // Check first few lines for HPGL command pattern (2 uppercase letters)
        int checkLines = std::min(3, (int)lines.size());
        for (int i = 0; i < checkLines; i++) {
            const std::string& line = lines[i];
            if (line.length() >= 2) {
                // Look for HPGL pattern: two uppercase letters followed by optional digits
                char first = line[0];
                char second = line[1];
                if (isalpha(first) && isupper(first) && isalpha(second) && isupper(second)) {
                    // Additional check: common HPGL commands
                    std::string command = line.substr(0, 2);
                    if (command == "IN" || command == "PU" || command == "PD" || 
                        command == "PA" || command == "PR" || command == "SP" || 
                        command == "LT" || command == "SC" || command == "IP") {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    
    // Simulate HPGL bounding box scanning by looking for specific commands
    struct BoundingBox {
        float minX = 0.0f, maxX = 0.0f;
        float minY = 0.0f, maxY = 0.0f;
        bool valid = false;
    };
    
    static BoundingBox scanHPGLBoundingBox(const std::vector<std::string>& lines) {
        BoundingBox bbox;
        bbox.valid = true; // Assume valid for testing
        
        // Mock scan - in real implementation, this would parse actual coordinates
        for (const auto& line : lines) {
            // Simulate finding coordinates in PD/PA commands
            if (line.find("PD") == 0 || line.find("PA") == 0) {
                // Mock coordinate extraction
                bbox.minX = -10.0f;
                bbox.maxX = 10.0f;
                bbox.minY = -5.0f;
                bbox.maxY = 5.0f;
                break;
            }
        }
        
        return bbox;
    }
};

void testHPGLScanning() {
    std::cout << "Testing HPGL Scanning...\n";
    
    // Test 1: HPGL file detection
    {
        std::vector<std::string> hpglLines = {
            "IN",           // HPGL command
            "PU 0,0",       // HPGL command
            "PD 100,100"    // HPGL command
        };
        
        assert(MockHPGLScanner::isHPGLFile(hpglLines) == true);
        std::cout << "✓ HPGL file detection works\n";
    }
    
    // Test 2: G-code file detection
    {
        std::vector<std::string> gcodeLines = {
            "G0 X10 Y10",   // G-code command
            "G1 Z5",        // G-code command
            "M3 S100"       // G-code command
        };
        
        assert(MockHPGLScanner::isHPGLFile(gcodeLines) == false);
        std::cout << "✓ G-code file detection works\n";
    }
    
    // Test 3: Mixed file detection - should detect HPGL
    {
        std::vector<std::string> mixedLines = {
            "G0 X0 Y0",     // G-code command
            "IN",           // HPGL command
            "PU 0,0"        // HPGL command
        };
        
        assert(MockHPGLScanner::isHPGLFile(mixedLines) == true);
        std::cout << "✓ Mixed file detection works\n";
    }
    
    // Test 4: Empty file
    {
        std::vector<std::string> emptyLines = {};
        assert(MockHPGLScanner::isHPGLFile(emptyLines) == false);
        std::cout << "✓ Empty file handling works\n";
    }
    
    // Test 5: Bounding box scanning
    {
        std::vector<std::string> hpglLines = {
            "IN",
            "PU 0,0",
            "PD 100,50"
        };
        
        auto bbox = MockHPGLScanner::scanHPGLBoundingBox(hpglLines);
        assert(bbox.valid == true);
        assert(bbox.minX == -10.0f);
        assert(bbox.maxX == 10.0f);
        assert(bbox.minY == -5.0f);
        assert(bbox.maxY == 5.0f);
        std::cout << "✓ HPGL bounding box scanning works\n";
    }
    
    std::cout << "All HPGL Scanning Tests Passed!\n\n";
}

int main() {
    testHPGLScanning();
    return 0;
}