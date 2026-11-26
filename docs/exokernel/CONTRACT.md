XASMOS exokernel contract (v0)
Principle

The kernel multiplexes physical resources and verifies capabilities. It does not name resources, it does not decide who gets what, it knows nothing about disk formats or specific programs. Everything that says WHO accesses WHAT lives in user space.
Kernel mechanisms (and only these)

    Physical frames: allocation and deallocation from a pool.
    Capabilities (tokens): create, derive (sub-range and sub-rights), transfer (requires GIVE), revoke (tree), release (requires RELEASE).
    Address spaces: map and unmap a frame using a MAP capability.
    Execution: launch a context with initial registers, yield, terminate.
    Interrupts and timer: delivery to whoever holds the vector capability.
    DMA mediation: only while there is no IOMMU. Validates descriptors with pins; decides nothing else.
    Roots: mint a capability over a physical resource (a device, a range of frames). Minting grants access to no one.

Forbidden in the kernel

    Resource or device names (rd0, nd0, sd0, |dev|, paths).
    Grant or policy tables per owner or per program.
    Quotas per owner (g_quota). Memory budget is a capability.
    File systems, volume formats, shell, install, format.
    Deciding capability recipients or fixed owners.

Memory budget

A capability to frames (a "pool") that the owner possesses. Allocating deducts from it; releasing returns to the same pool. Deriving sub-pools is attenuation. The kernel knows no limits per owner: it only verifies that the presented pool has frames and that the token is its own.
Revocation

Each capability entry stores its parent. Revoking a capability also withdraws all its descendants. Traversal bounded by the size of the table. A child without RELEASE is reclaimed by revoking its parent.
Where what was left out lives

    Volume mounting and reading: file system program (.exb).
    Shell and utilities: .exb.
    Initial root distribution: boot program (.exb), not the kernel.
    Console: delegated device capability, not a kernel call.

Declared debts

    DMA mediation without IOMMU (until AM5 with IOMMU).
    Serial boot diagnostic console, until the capability exists.

Ownership transfer (v1)

    An endpoint is an object owned by its receiver. It is created with two capabilities over it: RECV (to accept, kept by the receiver) and SEND (to offer, attenuated and delivered to whoever wants to send to it).
    Offer: the giver presents a token of theirs with GIVE and a SEND. The token becomes frozen in the endpoint's pending slot. Owner does not change.
    Accept: the receiver presents their RECV. The token passes to their owner.
    Cancel: the giver withdraws the offer; the token is unfrozen.
    One pending slot per endpoint. Destroying the endpoint cancels the offer.
    Unilateral transfer does not exist. xtok_give is retired.
