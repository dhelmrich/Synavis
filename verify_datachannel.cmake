# verify_datachannel.cmake
message("🔎 Checking for ${TARGET_NAME} in ${DEST_DIR}")
if(NOT EXISTS "${DEST_DIR}/${TARGET_NAME}")
  message(FATAL_ERROR "❌ ERROR: ${TARGET_NAME} not found in ${DEST_DIR}!")
else()
  message("✅ Found ${TARGET_NAME} in ${DEST_DIR}")
endif()
