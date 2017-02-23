/*SoC Porting information:
 */

/* Refer: Danube SoC Functional Spec
 *
 * The below values should be populated in the dtb file for SoC.
 *
 * Configure the RPU view addresses in dtb, they should directly accessible
 * using memory mapped io in Danube SoC.
 * (Ref: Volt UCCP.Technical Reference Manual.pdf)
 * This might change depending on the SoC.
 * Sample entries looks like below
 *	compatible = "img,pistachio-uccp";
 *	reg = <0x18480000 0x9000>, <0x184C0000 0x80000>,
 *	      <0x1a000000 0x00066CC0>;
 *	reg-names = "uccp_sysbus_base", "uccp_perip_base",
 *		    "uccp_pkd_gram_base";
 *	interrupts = <GIC_SHARED 67 IRQ_TYPE_LEVEL_HIGH>;
 *	interrupt-names = "uccpirq";
 * uccp_sysbus_base ==> Slave Host System Bus (IMG Bus 2.0 compliant)
 *	interface for connection to an external processor, allowing
 *	the external processor to access all internal RPU registers
 *	and GRAM.
 * uccp_perip_base ==> Master External System Bus (IMG Bus 1.0 compliant)
 *	interface for connection of peripherals that can be controlled by
 *	RPU’s internal META LTP processor.
 * uccp_pkd_gram_base ==> Byte Access of GRAM
 * uccp_gram_base ==>  Word Access of GRAM
 * uccpirq ==> IRQ Line assigned to RPU
 */
