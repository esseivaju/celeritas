.. Copyright Celeritas contributors: see top-level COPYRIGHT file for details
.. SPDX-License-Identifier: CC-BY-4.0

Celeritas setup
===============

The setup options help translate the Geant4 physics and problem setup to
Celeritas. They are also necessary to set up the GPU offloading
characteristics. Future versions of Celeritas will automate more of these
settings.

By default, sensitive detectors are automatically mapped from Geant4 to
Celeritas using the ``enabled`` option of
:cpp:struct:`celeritas::SDSetupOptions`. If no SDs are present (e.g., in a test
problem, or one which scores only through a user stepping action), the Celeritas
setup will fail with an error like:

.. code-block:: none

   *** G4Exception : celer0001
         issued by : accel/detail/GeantSd.cc:210
   Celeritas runtime error: no G4 sensitive detectors are defined: set `SetupOptions.sd.enabled` to `false` if this is expected
   *** Fatal Exception *** core dump ***


.. celerstruct:: SetupOptions
.. celerstruct:: SDSetupOptions

.. _geant_stepping_actions:

Registered stepping actions
---------------------------

Set ``SetupOptions.geant_stepping_actions = true`` to forward steps transported
by Celeritas to the actions already registered with Geant4. Framework input can
instead set ``inp::Problem::geant_stepping_actions`` in its problem adjustment
function. This flag defaults to false and is independent of sensitive detector
forwarding and the Celeritas ROOT MC truth writer. Applications without
sensitive detectors should also set ``SetupOptions.sd.enabled = false``.

Registration and ordering
^^^^^^^^^^^^^^^^^^^^^^^^^

Celeritas obtains the global stepping action from the owning worker's Geant4
event manager and invokes that registered object once per completed track step.
A composite action dispatches its own children in its normal order, including
nested composites. Celeritas then invokes the regional stepping action belonging
to the **pre-step** volume, including when the step crosses into another region
or leaves the world. Either action may be absent. Geant4 retains ownership of
both actions; no additional action registration is needed.

Sensitive detector callbacks run first, followed by the global and regional
stepping actions. The latter two receive the same reconstructed step. Stepping
actions also receive zero-deposition steps, steps in non-sensitive volumes,
boundary steps, and terminal steps. Inactive slots and warmup iterations do not
produce callbacks.

Callbacks execute on the owning Geant4 worker when ``Stepper::get()`` consumes
the completed iteration. This applies to both CPU and GPU transport:
``async()`` captures data, ``ready()`` polls transport and transfer completion,
and ``wait()`` waits for that completion. Neither polling nor waiting invokes
Geant4 callbacks. The synchronous stepping wrappers still submit and immediately
consume the result.

Each track's steps
retain their order, and a parent's creation-step callback precedes its children's
step callbacks. Ordering between unrelated tracks is unspecified and differs
from Geant4's sequential track scheduling.

Reconstructed data and ownership
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The reconstructed step exposes:

* Pre/post position, momentum direction, kinetic energy, global and local time,
  weight, physical/logical volumes, touchables, materials, material-cut couples,
  and sensitive detector pointers.
* Total energy deposition, step length, accumulated track length, and step
  number. Original Geant4 track length, step count, and local-time origin are
  retained when a track is offloaded.
* Particle definition, track/parent IDs, track status, and birth vertex
  information. Original Geant4 IDs are preserved; Celeritas descendants receive
  stable negative IDs unique across flushes within the framework event.
* The current step's surviving secondaries, with assigned identities and birth
  state, through ``GetSecondaryInCurrentStep()``. The list is empty on steps
  without births. ``GetSecondary()`` exposes this same current-step list, not
  cumulative secondary history.

Each transported track has a worker-local persistent Geant4 track object.
``G4VUserTrackInformation`` and auxiliary track information attached to an
offloaded track are transferred to that object. Information attached to a
secondary during its parent's callback remains
available during its own later callbacks and sensitive detector hits. The track
store owns the attached information and destroys it once during flush cleanup;
replacing an information pointer follows Geant4's normal ownership rules.
Track pointers, steps, step points, touchables, and secondary lists are borrowed.
Consumers must copy values they need beyond the callback; step objects and
touchable contents are reused. Cleanup and event transitions occur only after
transport and callbacks finish.

Only one iteration can be pending per worker. Applications may stage a later
primary batch while that iteration is pending. Normal callback completion waits
for the producing step's event, not subsequent stream work. ``get()`` must be
called on the owning worker before the next iteration, normal flush cleanup,
or an event transition. Calling ``wait()`` alone does not deliver callbacks;
destructors do not invoke them or add implicit synchronization.

Supported effects and limits
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Actions may observe the reconstructed state, write external output, and attach
or update user metadata. Transport-state changes, including killing,
suspension, postponement, changing kinematics or weights, and editing secondary
transport state, raise an error. The check occurs after the complete global and
regional chain. Callback exceptions propagate and permanently disable further
transport on that stepper. Repeated ``get()`` calls rethrow the saved exception
without repeating previously delivered sensitive-detector or stepping-action
callbacks. An undelivered batch cannot be overwritten. Submission failures also
disable reuse. Error cleanup finishes outstanding stream transfers before
propagating the failure so their buffers can be safely destroyed; it does not
dispatch a partial batch. Recovery or replay after failure is not supported.

This is a reconstruction interface, not a live Geant4 stepping manager.
Tracking-action lifecycle emulation, cumulative secondary history, and process
pointer reconstruction are unavailable. Post-step status distinguishes geometry
and world boundaries; other steps use ``fUserDefinedLimit`` rather than a Geant4
process classification. Pre-step process status, proper time, safety, velocity,
and nonionizing energy deposition are not reconstructed. An imported creator
process pointer can be retained, but Celeritas-born tracks have none.
The main transport loop is covered; optical-loop forwarding is a separate
extension.

Implementation and cost
^^^^^^^^^^^^^^^^^^^^^^^

A separately named, unfiltered collector saves completed steps at ``user_post``,
before transport slots can be reused. Secondary initialization emits optional
birth records at ID assignment, including children placed directly in a killed
parent's slot. A ``user_end`` action transfers these records after IDs have been
assigned. Sensitive-detector snapshots and unfiltered stepping snapshots use
separate reusable pinned buffers, allocated before transport starts.

Capture copies the selected full-capacity slot arrays on the producing stream,
without a host count query or a stream wait. The step-completion event follows
all step and birth transfers. After this event completes, host filtering removes
inactive and detector-filtered slots in place and reconstruction dispatches the
callbacks in their established order. Warmup retires empty batches without
calling Geant4.

Enabling forwarding adds full-step collection and transfer, host filtering,
touchable reconstruction, persistent host tracks, and user callback costs.
Full-capacity transfers trade bandwidth on sparse iterations for predictable
capture without a device compaction count roundtrip. Existing transport counter
synchronization, diagnostic synchronization, and unrelated step consumers can
still block ``async()``. Action and step timing include callback execution but
exclude application work between submission and consumption.

The default disabled configuration
creates no callback collector, worker processor, snapshot buffers, or secondary
birth buffers. Sensitive-detector snapshot buffers remain conditional on
sensitive-detector forwarding. Optical-loop forwarding is unchanged.

.. doxygenclass:: celeritas::GeantSteppingAction


Helper functions
----------------

This function helps find logical volumes from volume names:

.. doxygenfunction:: celeritas::FindVolumes

Magnetic field setup
--------------------

The magnetic fields defined for Celeritas can be used as Geant4 native magnetic
fields (see :ref:`api_accel_adapters`).

.. doxygenclass:: celeritas::UniformAlongStepFactory
.. doxygenclass:: celeritas::RZMapFieldAlongStepFactory
.. doxygenclass:: celeritas::CylMapFieldAlongStepFactory

.. _g4_ui_macros:

Macro UI Options
----------------

The :cpp:class:`celeritas::SetupOptionsMessenger`, instantiated automatically
by the Integration helper classes, provides a Geant4 "UI" macro interface to
many of the options.

.. doxygenclass:: celeritas::SetupOptionsMessenger
