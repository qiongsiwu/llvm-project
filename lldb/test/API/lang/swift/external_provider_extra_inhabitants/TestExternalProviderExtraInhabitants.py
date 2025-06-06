"""
Test that the external provider calculates the extra inhabitants of clang types correctly
"""
import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *


class TestExternalProviderExtraInhabitants(TestBase):

    @skipUnlessDarwin
    @swiftTest
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, 'Set breakpoint here.', lldb.SBFileSpec('main.swift'))

        self.expect('frame var object.size.width', substrs=['10'])
        self.expect('frame var object.size.height', substrs=['20'])

