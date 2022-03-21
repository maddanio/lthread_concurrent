#!/usr/bin/env python3
import requests
session = requests.Session()
while True:
	session.get("http://localhost:3128")
