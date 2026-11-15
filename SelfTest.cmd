@echo off
rem Scripted check: opens test_media, makes screenshots and writes test_media\selftest.log
start "" "%~dp0MediaSense.exe" --selftest "%~dp0test_media"
