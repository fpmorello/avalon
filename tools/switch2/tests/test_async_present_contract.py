# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3] / "src/video_core/renderer_vulkan"


def body(file, start, end):
    return (ROOT / file).read_text().split(start, 1)[1].split(end, 1)[0]


class AsyncPresentContract(unittest.TestCase):
    def test_horizon_selector_is_opt_in_and_applied_before_launch(self):
        main = (ROOT.parents[1] / "eden_horizon/main.cpp").read_text()
        self.assertRegex(main, r'\{"async_presentation", "Async presentation",[^{}]+\{"off", "on"\}, 0, true\}')
        self.assertRegex(main, r'option.key == "async_presentation"\) \{\s*Settings::values.async_presentation.SetValue\(choice == "on"\);')
        self.assertLess(main.index("ApplyOptions(effective_options)"), main.index("RunGame(", main.index("ApplyOptions(effective_options)")))

    def test_submission_and_shutdown_order(self):
        present = body("vk_present_manager.cpp", "void PresentManager::Present(Frame* frame)",
                       "size_t PresentManager::MaxExtraFrames")
        self.assertLess(present.index("present_queue.push_back(frame)"),
                        present.index("scheduler.DispatchWork()"))
        drain = body("vk_present_manager.cpp", "void PresentManager::StopPresentThread()",
                     "Frame* PresentManager::GetRenderFrame")
        self.assertLess(drain.index("scheduler.WaitWorker()"), drain.index("WaitPresent()"))
        self.assertLess(drain.index("WaitPresent()"), drain.index("present_thread.join()"))
        renderer = body("renderer_vulkan.cpp", "RendererVulkan::~RendererVulkan()",
                        "void RendererVulkan::Composite")
        self.assertLess(renderer.index("StopPresentThread()"), renderer.index("WaitIdle()"))
        idle = body("vk_blit_screen.cpp", "void BlitScreen::WaitIdle", "void BlitScreen::SetWindowAdaptPass")
        self.assertLess(idle.index("scheduler.Finish()"), idle.index("WaitPresent()"))

    def test_horizon_recreation_retires_gpu_work_in_both_modes(self):
        recreate = body("vk_present_manager.cpp", "void PresentManager::RecreateSwapchain",
                        "PresentManager::SurfaceState PresentManager::GetSurfaceState")
        horizon = recreate.split("#ifdef __SWITCH2__", 1)[1].split("#endif", 1)[0]
        self.assertNotIn("use_present_thread", horizon)
        self.assertLess(horizon.index("scheduler.submit_mutex"), horizon.index("WaitIdle()"))
        self.assertLess(recreate.index("WaitIdle()"), recreate.index("swapchain.Create"))

    def test_timed_out_frame_is_released_not_copied(self):
        copy = body("vk_present_manager.cpp", "void PresentManager::CopyToSwapchainImpl",
                    "const vk::CommandBuffer cmdbuf")
        timeout = copy.index("Acquisition::Unavailable")
        self.assertRegex(copy[timeout:], r"DiscardFrame\(frame\);\s*return;")
        acquire = body("vk_swapchain.cpp", "Swapchain::AcquireNextImage", "bool Swapchain::Present")
        self.assertLess(acquire.index("case VK_TIMEOUT:"), acquire.index("Acquisition::Unavailable"))
        self.assertLess(acquire.index("Acquisition::Unavailable"), acquire.index("resource_ticks[image_index]"))

    def test_present_out_of_date_is_checked_before_another_acquire(self):
        predicate = body("vk_swapchain.h", "bool NeedsRecreation() const",
                         "/// Returns true when the swapchain is outdated")
        self.assertIn("IsOutDated()", predicate)
        copy = body("vk_present_manager.cpp", "void PresentManager::CopyToSwapchainImpl",
                    "const vk::CommandBuffer cmdbuf")
        check = copy.index("swapchain.NeedsRecreation()")
        self.assertLess(copy.index("RecreateSwapchain(frame)", check),
                        copy.index("swapchain.AcquireNextImage"))

    def test_acquired_and_submitted_signals_have_separate_retirement(self):
        acquire = body("vk_swapchain.cpp", "Swapchain::AcquireNextImage", "bool Swapchain::Present")
        self.assertIn("return Acquisition::Suboptimal", acquire)
        copy = body("vk_present_manager.cpp", "void PresentManager::CopyToSwapchainImpl",
                    "const vk::CommandBuffer cmdbuf")
        self.assertLess(copy.index("RetireAcquisition()"), copy.index("Acquisition::Unavailable"))
        present = (ROOT / "vk_swapchain.cpp").read_text().split("bool Swapchain::Present", 1)[1]
        lost = present.split("case VK_ERROR_SURFACE_LOST_KHR:", 1)[1].split("default:", 1)[0]
        horizon_lost = lost.split("#ifdef __SWITCH2__", 1)[1].split("#else", 1)[0]
        self.assertIn("is_outdated = true", horizon_lost)
        self.assertNotIn("vk::Check", horizon_lost)
