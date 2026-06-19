// Test file for M0/M1 pause functionality
#include <unity.h>

// Mock implementation to test pause functionality
class MockGCodeParser {
public:
    enum State {
        IDLE,
        RUNNING,
        PAUSED,
        DWELL
    };
    
    State currentState = IDLE;
    bool pauseRequested = false;
    bool resumeRequested = false;
    
    // Simulate processing G-code command
    void processCommand(const std::string& command) {
        if (command == "M0" || command == "M1") {
            // Handle pause commands
            pauseRequested = true;
            currentState = PAUSED;
        } else if (command == "M114") {
            // Handle position reporting
            return;
        } else if (command == "M114") {
            // Handle position reporting
            return;
        } else if (command == "M3" || command == "M4" || command == "M5") {
            // Handle solenoid commands
            return;
        } else {
            // Handle normal movement commands
            if (currentState == IDLE || currentState == RUNNING) {
                currentState = RUNNING;
            }
        }
    }
    
    // Simulate resuming from pause
    void resumeFromPause() {
        if (currentState == PAUSED) {
            currentState = RUNNING;
            resumeRequested = true;
        }
    }
    
    // Simulate pausing
    void requestPause() {
        if (currentState == RUNNING) {
            currentState = PAUSED;
            pauseRequested = true;
        }
    }
    
    // Get current state for testing
    State getState() const { return currentState; }
    bool isPaused() const { return currentState == PAUSED; }
    bool isRunning() const { return currentState == RUNNING; }
    bool isIdle() const { return currentState == IDLE; }
};

void testPauseFunctionality() {
    MockGCodeParser parser;
    
    // Test 1: Initial state
    TEST_ASSERT_TRUE(parser.isIdle());
    TEST_ASSERT_FALSE(parser.isRunning());
    TEST_ASSERT_FALSE(parser.isPaused());
    
    // Test 2: Process normal command
    parser.processCommand("G0 X10 Y10");
    TEST_ASSERT_TRUE(parser.isRunning());
    
    // Test 3: Process pause command
    parser.processCommand("M0");
    TEST_ASSERT_TRUE(parser.isPaused());
    TEST_ASSERT_TRUE(parser.pauseRequested);
    
    // Test 4: Resume from pause
    parser.resumeFromPause();
    TEST_ASSERT_TRUE(parser.isRunning());
    TEST_ASSERT_TRUE(parser.resumeRequested);
    
    // Test 5: Process M1 command (same as M0)
    parser.processCommand("G1 X20 Y20");
    parser.processCommand("M1");
    TEST_ASSERT_TRUE(parser.isPaused());
    
    // Test 6: Process position command while paused
    parser.processCommand("M114"); 
    TEST_ASSERT_TRUE(parser.isPaused());
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(testPauseFunctionality);
    UNITY_END();
}

void loop() {
    delay(1000);
}