#include <unity.h>
// ... mock class and test functions ...

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
    // Test 1: Scale transform
    {
        std::string transform = "scale(2, 1.5)";
        auto result = MockSVGParser::parseTransform(transform);
        TEST_ASSERT_EQUAL_FLOAT(2.0f, result.scaleX);
        TEST_ASSERT_EQUAL_FLOAT(1.5f, result.scaleY);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, result.rotate);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, result.translateX);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, result.translateY);
    }
    
    // Test 2: Rotate transform
    {
        std::string transform = "rotate(45)";
        auto result = MockSVGParser::parseTransform(transform);
        TEST_ASSERT_EQUAL_FLOAT(45.0f, result.rotate);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, result.scaleX);
    }
    
    // Test 3: Translate transform
    {
        std::string transform = "translate(10, 20)";
        auto result = MockSVGParser::parseTransform(transform);
        TEST_ASSERT_EQUAL_FLOAT(10.0f, result.translateX);
        TEST_ASSERT_EQUAL_FLOAT(20.0f, result.translateY);
    }
    
    // Test 4: Combined transforms
    {
        std::string transform = "translate(10, 20) scale(2, 1.5) rotate(45)";
        auto result = MockSVGParser::parseTransform(transform);
        TEST_ASSERT_EQUAL_FLOAT(10.0f, result.translateX);
        TEST_ASSERT_EQUAL_FLOAT(20.0f, result.translateY);
        TEST_ASSERT_EQUAL_FLOAT(2.0f, result.scaleX);
        TEST_ASSERT_EQUAL_FLOAT(1.5f, result.scaleY);
        TEST_ASSERT_EQUAL_FLOAT(45.0f, result.rotate);
    }
    
    // Test 5: Valid transform detection
    {
        TEST_ASSERT_TRUE(MockSVGParser::isValidTransform("scale(2, 1.5)"));
        TEST_ASSERT_TRUE(MockSVGParser::isValidTransform("rotate(45)"));
        TEST_ASSERT_TRUE(MockSVGParser::isValidTransform("translate(10, 20)"));
        TEST_ASSERT_FALSE(MockSVGParser::isValidTransform(""));
        TEST_ASSERT_FALSE(MockSVGParser::isValidTransform("invalid"));
    }
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(testSvgTransformParsing);
    UNITY_END();
}

void loop() {
    delay(1000);
}