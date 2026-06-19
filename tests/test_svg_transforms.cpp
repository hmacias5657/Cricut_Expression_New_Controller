// Test file for SVG transform parsing functionality
#include <iostream>
#include <string>
#include <vector>
#include <cassert>

// Mock minimal implementation to test transform parsing logic
class MockSVGParser {
public:
    // Simulate transform parsing - extract scale, rotate, translate values
    struct Transform {
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float rotate = 0.0f;
        float translateX = 0.0f;
        float translateY = 0.0f;
    };
    
    // Simple parser for transform attributes
    static Transform parseTransform(const std::string& transformStr) {
        Transform t;
        
        // Handle scale(x,y) - simplified
        size_t scalePos = transformStr.find("scale(");
        if (scalePos != std::string::npos) {
            size_t start = scalePos + 6; // "scale("
            size_t end = transformStr.find(')', start);
            if (end != std::string::npos) {
                std::string scaleParams = transformStr.substr(start, end - start);
                size_t commaPos = scaleParams.find(',');
                if (commaPos != std::string::npos) {
                    t.scaleX = std::stof(scaleParams.substr(0, commaPos));
                    t.scaleY = std::stof(scaleParams.substr(commaPos + 1));
                } else {
                    t.scaleX = t.scaleY = std::stof(scaleParams);
                }
            }
        }
        
        // Handle rotate(angle) - simplified
        size_t rotatePos = transformStr.find("rotate(");
        if (rotatePos != std::string::npos) {
            size_t start = rotatePos + 7; // "rotate("
            size_t end = transformStr.find(')', start);
            if (end != std::string::npos) {
                t.rotate = std::stof(transformStr.substr(start, end - start));
            }
        }
        
        // Handle translate(x,y) - simplified
        size_t transPos = transformStr.find("translate(");
        if (transPos != std::string::npos) {
            size_t start = transPos + 10; // "translate("
            size_t end = transformStr.find(')', start);
            if (end != std::string::npos) {
                std::string transParams = transformStr.substr(start, end - start);
                size_t commaPos = transParams.find(',');
                if (commaPos != std::string::npos) {
                    t.translateX = std::stof(transParams.substr(0, commaPos));
                    t.translateY = std::stof(transParams.substr(commaPos + 1));
                } else {
                    t.translateX = std::stof(transParams);
                    t.translateY = 0.0f;
                }
            }
        }
        
        return t;
    }
    
    // Test whether transformation would be detected
    static bool isValidTransform(const std::string& transformStr) {
        return !transformStr.empty() && 
               (transformStr.find("scale(") != std::string::npos ||
                transformStr.find("rotate(") != std::string::npos ||
                transformStr.find("translate(") != std::string::npos);
    }
};

void testSvgTransformParsing() {
    std::cout << "Testing SVG Transform Parsing...\n";
    
    // Test 1: Scale transform
    {
        std::string transform = "scale(2, 1.5)";
        auto result = MockSVGParser::parseTransform(transform);
        assert(result.scaleX == 2.0f);
        assert(result.scaleY == 1.5f);
        assert(result.rotate == 0.0f);
        assert(result.translateX == 0.0f);
        assert(result.translateY == 0.0f);
        std::cout << "✓ Scale transform parsing works\n";
    }
    
    // Test 2: Rotate transform
    {
        std::string transform = "rotate(45)";
        auto result = MockSVGParser::parseTransform(transform);
        assert(result.rotate == 45.0f);
        assert(result.scaleX == 1.0f);
        std::cout << "✓ Rotate transform parsing works\n";
    }
    
    // Test 3: Translate transform
    {
        std::string transform = "translate(10, 20)";
        auto result = MockSVGParser::parseTransform(transform);
        assert(result.translateX == 10.0f);
        assert(result.translateY == 20.0f);
        std::cout << "✓ Translate transform parsing works\n";
    }
    
    // Test 4: Combined transforms
    {
        std::string transform = "translate(10, 20) scale(2, 1.5) rotate(45)";
        auto result = MockSVGParser::parseTransform(transform);
        assert(result.translateX == 10.0f);
        assert(result.translateY == 20.0f);
        assert(result.scaleX == 2.0f);
        assert(result.scaleY == 1.5f);
        assert(result.rotate == 45.0f);
        std::cout << "✓ Combined transform parsing works\n";
    }
    
    // Test 5: Valid transform detection
    {
        assert(MockSVGParser::isValidTransform("scale(2, 1.5)"));
        assert(MockSVGParser::isValidTransform("rotate(45)"));
        assert(MockSVGParser::isValidTransform("translate(10, 20)"));
        assert(!MockSVGParser::isValidTransform(""));
        assert(!MockSVGParser::isValidTransform("invalid"));
        std::cout << "✓ Transform validation works\n";
    }
    
    std::cout << "All SVG Transform Tests Passed!\n\n";
}

int main() {
    testSvgTransformParsing();
    return 0;
}