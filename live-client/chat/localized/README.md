# Flat localized float messages

The native formatter now handles the authored `%DF` token in a flat localized
message. It looks up the format in the contributor's own English STF table,
uses the existing U/T/O object context for any other supported tokens, and
substitutes a finite wire float as a shortest, locale-independent decimal.
For example, a synthetic `Your lap took %DF seconds.` format with `12.5`
becomes `Your lap took 12.5 seconds.` A synthetic third-person format can
combine `%TU` with `%DF` when the source name is known. Substituted text is
not scanned again.

A missing name can resolve on a later presentation pass. Until then, the HUD
shows `A localized message is unavailable.` instead of dropping the line.
Malformed table references, unknown tokens, nonfinite floats, unsupported
perspective segments, and outputs exceeding 8,192 UTF-16 units use the same
short fallback and remain eligible for a later retry. Successful text remains
stable through resource-cache eviction. The fallback contains no server,
account, or participant data.

This slice does not claim exact original `UIUtils::FormatFloat` precision for
all values, nor does it select original social/combat perspective segments.
Those need separate evidence and tests. The source-only synthetic tests use a
generated STF byte buffer; no original localization strings are shipped.
