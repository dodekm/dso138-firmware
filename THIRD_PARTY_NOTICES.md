# Licenses and attribution

The independently implemented oscilloscope application, C++ board initialization,
interrupt bindings, project HAL configuration, linker script and original
documentation are provided under the [MIT License](LICENSE).

Third-party components retain their own licenses. The project MIT grant does
not replace their notices or apply a different license to vendor files.

| Component | Location | License |
| --- | --- | --- |
| STM32F1 HAL | `firmware/Drivers/STM32F1xx_HAL_Driver/` | [BSD 3-Clause](firmware/Drivers/STM32F1xx_HAL_Driver/LICENSE.md) |
| Arm CMSIS core | `firmware/Drivers/CMSIS/` core headers | [Apache 2.0](firmware/Drivers/CMSIS/LICENSE.txt) |
| ST CMSIS device | `firmware/Drivers/CMSIS/Device/ST/STM32F1xx/` | [Apache 2.0](firmware/Drivers/CMSIS/Device/ST/STM32F1xx/License.md) |
| Cortex-M3 startup assembly and CMSIS system implementation | `firmware/Startup/`, `firmware/Src/` | Supplied ST source notices and Apache 2.0; license copies are retained in both directories |
| GNU compiler runtime, when linked by the toolchain | External toolchain | [GPL 3 with GCC Runtime Library Exception 3.1](licenses/runtime/GPL-3.0-with-GCC-Runtime-Library-Exception-3.1.txt) |
| Newlib routines, when linked by the toolchain | External toolchain | [Retained component notices](licenses/runtime/Newlib-linked-members-notices.txt) |

## Vendor provenance

The HAL snapshot corresponds to commit
`77fbb30b7a1d02533980400083e48c559aae5a4f` of
[ST's STM32F1 HAL driver](https://github.com/STMicroelectronics/stm32f1xx-hal-driver).
The CMSIS device snapshot corresponds to commit
`8a76309ed1250d817e9c888c4417171d2ba3ba63` of
[ST's CMSIS device F1 component](https://github.com/STMicroelectronics/cmsis-device-f1).
The configuration targets the STM32CubeF1 1.8.6 package family.

The startup assembly matches the component's GCC `startup_stm32f103xb.s`
template. The system implementation retains its supplied ST notice; an exact
source-revision match for that file is not asserted.

The public source uses independently authored C++ HAL initialization and IRQ
bindings rather than redistributing the earlier CubeMX-generated application
initialization files with unresolved component-root terms. The application
does not allocate a heap or use console I/O. Its small project-owned Newlib
I/O bindings return an error for unsupported file operations.

No JYE Tech proprietary capture library, original firmware binary or DLO-138
implementation is included. The independently implemented glyphs and waveform
illustrations belong to this project. This is an independent project, not an
official JYE Tech release.

Keep the applicable notices when redistributing source or compiled firmware.
