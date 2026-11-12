cmake_minimum_required(VERSION 3.20)
project(recommendation-service VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Drogon CONFIG REQUIRED)

add_library(recommendation-core
    recommendation/domain/OutfitPolicy.cpp
    recommendation/application/RecommendationService.cpp
    recommendation/application/ConversationPolicy.cpp
)
target_compile_features(recommendation-core PUBLIC cxx_std_17)
set_target_properties(recommendation-core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(recommendation-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(recommendation-service
    main.cpp
    recommendation/infrastructure/FixtureWardrobeClient.cpp
    recommendation/infrastructure/DigitalClosetWardrobeClient.cpp
    recommendation/infrastructure/GarmentJson.cpp
    recommendation/infrastructure/StyleKnowledgeJson.cpp
    recommendation/infrastructure/OutfitExampleJson.cpp
    recommendation/infrastructure/IamIdentityVerifier.cpp
    recommendation/infrastructure/IamProfileClient.cpp
    recommendation/infrastructure/NvidiaRecommenderAgent.cpp
    recommendation/infrastructure/ConversationTools.cpp
    recommendation/infrastructure/FashionWebSearch.cpp
    recommendation/infrastructure/WardrobeIndex.cpp
    recommendation/infrastructure/SupabaseRecommendationRepository.cpp
    recommendation/interfaces/RecommendationHttp.cpp
    recommendation/interfaces/ApiDocumentation.cpp
)
target_link_libraries(recommendation-service PRIVATE recommendation-core Drogon::Drogon PostgreSQL::PostgreSQL)
target_compile_definitions(recommendation-service PRIVATE
    RECOMMENDATION_FIXTURE_PATH="${CMAKE_CURRENT_BINARY_DIR}/fixtures/garments.json"
    RECOMMENDATION_DOCS_PATH="${CMAKE_CURRENT_BINARY_DIR}/docs")
configure_file(fixtures/garments.json fixtures/garments.json COPYONLY)
foreach(asset index.html openapi.json swagger-ui.css swagger-ui-bundle.js swagger-initializer.js LICENSE VENDOR.md)
    configure_file("docs/${asset}" "${CMAKE_CURRENT_BINARY_DIR}/docs/${asset}" COPYONLY)
endforeach()

if(MSVC)
    target_compile_options(recommendation-core PRIVATE /W4 /utf-8)
    target_compile_options(recommendation-service PRIVATE /W4 /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(recommendation-tests tests/recommendation_tests.cpp)
    target_link_libraries(recommendation-tests PRIVATE recommendation-core)
    add_test(NAME recommendation-rules COMMAND recommendation-tests)
    add_executable(conversation-policy-tests tests/conversation_policy_tests.cpp)
    target_link_libraries(conversation-policy-tests PRIVATE recommendation-core)
    add_test(NAME recommendation-conversation-policy COMMAND conversation-policy-tests)

    add_executable(outfit-example-json-tests tests/outfit_example_json_tests.cpp recommendation/infrastructure/OutfitExampleJson.cpp)
    target_include_directories(outfit-example-json-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(outfit-example-json-tests PRIVATE Drogon::Drogon)
    if(MSVC)
        target_compile_options(outfit-example-json-tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME recommendation-example-json-guards COMMAND outfit-example-json-tests)
    add_executable(fashion-web-tests tests/fashion_web_tests.cpp recommendation/infrastructure/FashionWebSearch.cpp)
    target_include_directories(fashion-web-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(fashion-web-tests PRIVATE Drogon::Drogon)
    if(MSVC)
        target_compile_options(fashion-web-tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME recommendation-fashion-web-guards COMMAND fashion-web-tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME recommendation-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_smoke.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-http PROPERTIES TIMEOUT 30)
        add_test(NAME recommendation-history-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/history_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-history-http PROPERTIES TIMEOUT 30)
        add_test(NAME recommendation-nvidia-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/nvidia_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-nvidia-http PROPERTIES TIMEOUT 45)
        add_test(NAME recommendation-conversation-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/conversation_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-conversation-http PROPERTIES TIMEOUT 45)
        add_test(NAME recommendation-memory-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/memory_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-memory-http PROPERTIES TIMEOUT 60)
        add_test(NAME recommendation-tools-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-tools-http PROPERTIES TIMEOUT 60)
        add_test(NAME recommendation-fashion-web-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fashion_web_http.py" "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-fashion-web-http PROPERTIES TIMEOUT 60)
        add_test(NAME recommendation-rag-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rag_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-rag-contract PROPERTIES TIMEOUT 30)
        add_test(NAME recommendation-style-knowledge-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/style_knowledge_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-style-knowledge-http PROPERTIES TIMEOUT 30)
        add_test(NAME recommendation-polyvore-import COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/polyvore_import_test.py")
        add_test(NAME recommendation-polyvore-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/polyvore_http.py" "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-polyvore-http PROPERTIES TIMEOUT 30)
        add_test(NAME recommendation-closet-http
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/closet_http.py"
            "$<TARGET_FILE:recommendation-service>")
        set_tests_properties(recommendation-closet-http PROPERTIES TIMEOUT 60)
    endif()
endif()
