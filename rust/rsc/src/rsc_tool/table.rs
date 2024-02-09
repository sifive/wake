use papergrid::{
    colors::NoColors,
    config::{
        spanned::SpannedConfig, AlignmentHorizontal, AlignmentVertical, Borders, Entity, Indent,
        Sides,
    },
    dimension::{spanned::SpannedGridDimension, Estimate},
    grid::peekable::PeekableGrid,
    records::vec_records::{CellInfo, VecRecords},
};

pub fn print_table(data: Vec<Vec<String>>) {
    let mut cfg = SpannedConfig::default();
    cfg.set_borders(Borders {
        top: Some('─'),
        top_left: Some('╭'),
        top_right: Some('╮'),
        top_intersection: Some('┬'),
        bottom: Some('─'),
        bottom_left: Some('╰'),
        bottom_right: Some('╯'),
        bottom_intersection: Some('┴'),
        horizontal: Some('─'),
        left_intersection: Some('├'),
        right_intersection: Some('┤'),
        vertical: Some('│'),
        left: Some('│'),
        right: Some('│'),
        intersection: Some('┼'),
    });
    cfg.set_padding(
        Entity::Global,
        Sides::new(
            Indent::spaced(1),
            Indent::spaced(1),
            Indent::spaced(1),
            Indent::spaced(1),
        ),
    );

    cfg.set_alignment_horizontal(Entity::Global, AlignmentHorizontal::Left);
    cfg.set_alignment_horizontal(Entity::Row(0), AlignmentHorizontal::Center);
    cfg.set_alignment_vertical(Entity::Global, AlignmentVertical::Center);
    let data = data
        .iter()
        .map(|row| row.iter().map(CellInfo::new).collect())
        .collect();
    let records = VecRecords::new(data);
    let mut dims = SpannedGridDimension::default();
    dims.estimate(&records, &cfg);
    let grid = PeekableGrid::new(&records, &cfg, &dims, NoColors).to_string();
    println!("{grid}");
}
