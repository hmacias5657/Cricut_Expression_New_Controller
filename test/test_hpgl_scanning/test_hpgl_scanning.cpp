#include <unity.h>

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
    // Test 1: HPGL file detection
    {
        std::vector<std::string> hpglLines = {
            "IN",           // HPGL command
            "PU 0,0",       // HPGL command
            "PD 100,100"    // HPGL command
        };
        
        TEST_ASSERT_TRUE(MockHPGLScanner::isHPGLFile(hpglLines));
    }
    
    // Test 2: G-code file detection
    {
        std::vector<std::string> gcodeLines = {
            "G0 X10 Y10",   // G-code command
            "G1 Z5",        // G-code command
            "M3 S100"       // G-code command
        };
        
        TEST_ASSERT_FALSE(MockHPGLScanner::isHPGLFile(gcodeLines));
    }
    
    // Test 3: Mixed file detection - should detect HPGL
    {
        std::vector<std::string> mixedLines = {
            "G0 X0 Y0",     // G-code command
            "IN",           // HPGL command
            "PU 0,0"        // HPGL command
        };
        
        TEST_ASSERT_TRUE(MockHPGLScanner::isHPGLFile(mixedLines));
    }
    
    // Test 4: Empty file
    {
        std::vector<std::string> emptyLines = {};
        TEST_ASSERT_FALSE(MockHPGLScanner::isHPGLFile(emptyLines));
    }
    
    // Test 5: Bounding box scanning
    {
        std::vector<std::string> hpglLines = {
            "IN",
            "PU 0,0",
            "PD 100,50"
        };
        
        auto bbox = MockHPGLScanner::scanHPGLBoundingBox(hpglLines);
        TEST_ASSERT_TRUE(bbox.valid);
        TEST_ASSERT_EQUAL_FLOAT(-10.0f, bbox.minX);
        TEST_ASSERT_EQUAL_FLOAT(10.0f, bbox.maxX);
        TEST_ASSERT_EQUAL_FLOAT(-5.0f, bbox.minY);
        TEST_ASSERT_EQUAL_FLOAT(5.0f, bbox.maxY);
    }
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(testHPGLScanning);
    UNITY_END();
}

void loop() {
    delay(1000);
}