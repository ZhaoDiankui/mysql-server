# WeSQL

WeSQL is an innovative MySQL distribution that adopts a compute-storage separation architecture, with storage backed by S3 (and S3-compatible systems).
It can run on any cloud, ensuring no vendor lock-in.

WeSQL has completely replaced MySQL’s traditional disk storage with S3. All MySQL data—binlogs, schemas, storage engine metadata, WAL, and data files—are **entirely** (**not partially!**) 
stored as objects in S3.
The 11 nines of durability provided by S3 significantly enhances data reliability.
Additionally, WeSQL can start from a clean, empty instance, connect to S3, load the data, and begin serving immediately with no additional setup required.

It is ideal for users who need an easy-to-manage, cost-effective, and developer-friendly MySQL database solution, 
especially for those needing support for both Serverless and BYOC (Bring Your Own Cloud).

Feel free to start trying WeSQL in your development and testing environments!

## How to build

[Compile](https://wesql.io/docs/tutorial/binary/install)

## Docs with WeSQL

[Architecture](https://wesql.io/docs/architecture)

[Tutorial](https://wesql.io/docs/tutorial)

For further information on WeSQL or additional documentation, visit
  <https://wesql.io/docs/introduction>

## Community
Join our [Discord](https://discord.com/channels/1308609231498510427/1308609231498510430) to discuss features, get help, and connect with other users.

## Licensing

Portions Copyright (c) 2024, ApeCloud Inc Holding Limited. WeSQL is specifically available only under version 2 of the GNU General Public License (GPLv2). (I.e. Without the "any later version" clause.) This is inherited from MySQL. Please see the README file in the MySQL distribution for more information.
