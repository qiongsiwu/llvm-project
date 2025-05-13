import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil
import os
import platform

class TestStepThroughAllocatingInit(lldbtest.TestBase):
    mydir = lldbtest.TestBase.compute_mydir(__file__)

    @swiftTest
    def test_swift_stepping_api(self):
        """Test that step in using the Python API steps through thunk."""
        self.build()
        self.do_test(True)

    @swiftTest
    def test_swift_stepping_cli(self):
        """Same test with the cli - it goes a slightly different path than
           the API."""
        self.build()
        self.do_test(False)

    def setUp(self):
        lldbtest.TestBase.setUp(self)
        self.main_source = "main.swift"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)

    def do_test(self, use_api):
        """Tests that we can step reliably in swift code."""
        exe_name = "a.out"
        exe = self.getBuildArtifact(exe_name)

        target, process, thread, breakpoint = lldbutil.run_to_source_breakpoint(self,
        'Break here to step into init', self.main_source_spec)

        # Step into the function.
        if use_api:
            thread.StepInto()
        else:
            self.runCmd("thread step-in")
        frame_0 = thread.frames[0]
        self.assertIn('Foo.init()', frame_0.GetFunctionName())

        # Check that our parent frame is indeed allocating_init (otherwise we aren't
        # testing what we think we're testing...
        frame_1 = thread.frames[1]
        self.assertIn("allocating_init", frame_1.GetFunctionName())

        # Step one line so some_string is initialized, make sure we can
        # get its value:
        if use_api:
            thread.StepOver()
        else:
            self.runCmd("thread step-over")

        frame_0 = thread.frames[0]
        self.assertIn('Foo.init()', frame_0.GetFunctionName())
        var = frame_0.FindVariable("some_string")
        self.assertTrue(var.GetError().Success())
        self.assertEqual(var.GetSummary(), '"foo"')
        
        # Now make sure that stepping out steps past the thunk:
        if use_api:
            thread.StepOut()
        else:
            self.runCmd("thread step-out")

        frame_0 = thread.frames[0]
        self.assertIn("doSomething", frame_0.GetFunctionName())
