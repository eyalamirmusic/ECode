# Two applications built from the same libraries, which is the point of there
# being two:
#
#   ECode       the editor — links ECodeWorkbench, so tabs, sidebar, palette
#   CodeViewer  a single file — links ECodeEditor, so none of that
#
# CodeViewer is the app to reach for when changing anything in ECodeEditor or
# below. It exercises the embedding API by hand, which no test does: a test
# drives the class, and what a consumer actually meets is a window, a menu bar
# and a file that has to appear in it.
add_subdirectory(ECode)
add_subdirectory(CodeViewer)
