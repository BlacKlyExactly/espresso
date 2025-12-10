#!/bin/bash

BASE_URL="http://localhost:8080"

echo "===== Test root / ====="
curl -i $BASE_URL/
echo -e "\n"

echo "===== Test /api/ ====="
curl -i $BASE_URL/api/
echo -e "\n"

echo "===== Test /api/echo without query ====="
curl -i $BASE_URL/api/echo
echo -e "\n"

echo "===== Test /api/echo with query ====="
curl -i "$BASE_URL/api/echo?msg=HelloWorld"
echo -e "\n"

echo "===== Test CORS preflight OPTIONS ====="
curl -i -X OPTIONS $BASE_URL/api/echo
echo -e "\n"
