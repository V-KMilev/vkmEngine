# A vkmEngine project: every .cpp under src/, built into the module a host loads.
# `vkm build` runs this for you.
cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES CXX)

find_package(vkmEngine REQUIRED)

vkm_add_gameplay_module(game_module)
